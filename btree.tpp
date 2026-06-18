#ifndef BTREE_TPP
#define BTREE_TPP

#include <iostream>
#include <fstream>

// Implementacao de BTree. Incluido por btree.h.

// ===========================================================================
// CONSTRUCAO / DESTRUICAO / PERSISTENCIA DO META
// ===========================================================================
template <typename KeyType, int ORDER>
BTree<KeyType, ORDER>::BTree(const std::string& filename, int maxCacheSize,
                             SplitPolicy policy, DeletePolicy delPolicy,
                             bool reuseNodes)
    : filename_(filename),
      io_(filename),                 // abre/cria o .dat e calcula nextIdx
      pool_(io_, maxCacheSize),      // cache sobre o DiskIO
      rootIdx_(0),
      policy_(policy),
      delPolicy_(delPolicy),
      reuseNodes_(reuseNodes) {
    // Recupera a raiz e a free-list de uma arvore previamente persistida.
    // Formato do .meta:  <rootIdx>\n <qtdLivres>\n <idx idx ...>
    // (formato antigo, so com rootIdx, e' lido de forma compativel.)
    std::ifstream meta(filename_ + ".meta");
    if (meta) {
        meta >> rootIdx_;
        size_t cnt;
        if (meta >> cnt) {
            freeList_.clear();
            freeList_.reserve(cnt);
            for (size_t k = 0; k < cnt; k++) {
                int idx;
                if (meta >> idx) freeList_.push_back(idx);
            }
        }
    }
}

template <typename KeyType, int ORDER>
BTree<KeyType, ORDER>::~BTree() {
    pool_.flush();                              // grava nos dirty
    std::ofstream meta(filename_ + ".meta");    // persiste raiz + free-list
    meta << rootIdx_ << "\n";
    meta << freeList_.size() << "\n";
    for (int idx : freeList_) meta << idx << " ";
    meta << "\n";
    // pool_ e io_ sao destruidos em seguida (flush final + close).
}

// ===========================================================================
// FREE-LIST: alocacao e liberacao de slots fisicos.
//   allocateNode() reaproveita um slot livre (LIFO) se houver e o reuso
//   estiver ligado; caso contrario cresce o arquivo (pool_.allocate()).
//   freeNode() descarta o no do cache (sem writeback) e, se o reuso estiver
//   ligado, devolve o slot para a free-list. Com reuso desligado, o slot e'
//   simplesmente abandonado (orfao) — util para medir a ocupacao sem reuso.
// ===========================================================================
template <typename KeyType, int ORDER>
typename BTree<KeyType, ORDER>::Node* BTree<KeyType, ORDER>::allocateNode() {
    if (reuseNodes_ && !freeList_.empty()) {
        int idx = freeList_.back();
        freeList_.pop_back();
        return pool_.allocateAt(idx);
    }
    return pool_.allocate();
}

template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::freeNode(int diskIdx) {
    pool_.evict(diskIdx);
    if (reuseNodes_) freeList_.push_back(diskIdx);
}

// ===========================================================================
// INSERCAO  —  ponto de entrada: despacha pela estrategia escolhida.
// ===========================================================================
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::insert(KeyType key) {
    // CASO 0 (comum as duas estrategias): arvore vazia -> cria a raiz folha.
    if (this->rootIdx_ == 0) {
        Node* newRoot = this->allocateNode();
        this->rootIdx_ = newRoot->diskIdx;
        newRoot->keys[0] = key;
        newRoot->n = 1;
        this->pool_.markDirty(newRoot);
        return;
    }

    if (policy_ == SplitPolicy::Reactive) {
        // -------- ESTRATEGIA REATIVA (bottom-up / convencional) --------
        // Desce, insere na folha e deixa o split propagar de baixo para cima.
        InsertResult r = insertReactive(this->rootIdx_, key);
        if (r.promoted) {
            // A raiz transbordou: cresce a arvore criando uma nova raiz com a
            // chave promovida e os dois filhos (raiz antiga + novo no direito).
            int oldRootIdx = this->rootIdx_;
            Node* newRoot = this->allocateNode();
            int newRootIdx = newRoot->diskIdx;
            this->pool_.pin(newRootIdx);
            newRoot = this->pool_.fetch(newRootIdx);
            newRoot->keys[0]     = r.upKey;
            newRoot->n           = 1;
            newRoot->filhoIdx[0] = oldRootIdx;
            newRoot->filhoIdx[1] = r.rightIdx;
            this->pool_.markDirty(newRoot);
            this->pool_.unpin(newRootIdx);
            this->rootIdx_ = newRootIdx;
        }
        return;
    }

    // -------- ESTRATEGIA PREEMPTIVA (top-down / estilo CLRS) --------
    // CASO 2: raiz cheia -> cresce a arvore (split da raiz) antes de descer.
    this->pool_.pin(this->rootIdx_);
    Node* root = this->pool_.fetch(this->rootIdx_);
    if (root->n == ORDER - 1) {
        int oldRootIdx = this->rootIdx_;

        Node* newRoot = this->allocateNode();
        int newRootIdx = newRoot->diskIdx;
        this->pool_.pin(newRootIdx);

        newRoot->filhoIdx[0] = oldRootIdx;
        this->pool_.markDirty(newRoot);

        this->rootIdx_ = newRootIdx;
        this->splitChild(oldRootIdx, newRootIdx, 0);

        this->pool_.unpin(oldRootIdx);
        this->pool_.unpin(newRootIdx);
        this->insertNonFull(newRootIdx, key);
        return;
    }
    this->pool_.unpin(this->rootIdx_);

    // CASO 3: raiz tem espaco.
    this->insertNonFull(this->rootIdx_, key);
}

// Insere 'key' numa subarvore enraizada em nodeIdx que NAO esta cheia.
// [ESTRATEGIA PREEMPTIVA] Faz split preemptivo de qualquer filho cheio no
// caminho de descida, de modo que a propagacao de split nunca sobe.
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::insertNonFull(int nodeIdx, KeyType key) {
    pool_.pin(nodeIdx);
    Node* node = pool_.fetch(nodeIdx);
    int i = node->n - 1;

    if (node->isLeaf()) {
        // Insere na posicao ordenada e termina.
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->n++;
        pool_.markDirty(node);
        pool_.unpin(nodeIdx);
        return;
    }

    // No interno: localiza o filho que recebera a chave.
    while (i >= 0 && key < node->keys[i]) i--;
    i++;
    int childIdx = node->filhoIdx[i];

    // Split preemptivo se o filho estiver cheio.
    pool_.pin(childIdx);
    Node* child = pool_.fetch(childIdx);
    if (child->n == ORDER - 1) {
        splitChild(childIdx, nodeIdx, i);
        node = pool_.fetch(nodeIdx);   // pinado: cache hit
        if (key > node->keys[i]) {     // a chave promovida desviou o caminho
            pool_.unpin(childIdx);
            i++;
            childIdx = node->filhoIdx[i];
        } else {
            pool_.unpin(childIdx);
        }
    } else {
        pool_.unpin(childIdx);
    }

    // Descida em tail call: libera o pai antes de recursar.
    pool_.unpin(nodeIdx);
    insertNonFull(childIdx, key);
}

// Divide o filho cheio nodeIdx (que e' o filho i de paiIdx) em dois.
// Promove a chave do meio para o pai. Pre: nodeIdx e paiIdx pinados.
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::splitChild(int nodeIdx, int paiIdx, int i) {
    pool_.pin(nodeIdx);
    pool_.pin(paiIdx);

    Node* newNode = allocateNode();
    int newNodeIdx = newNode->diskIdx;
    pool_.pin(newNodeIdx);

    Node* node = pool_.fetch(nodeIdx);
    Node* pai  = pool_.fetch(paiIdx);

    const int mid  = (ORDER - 1) / 2;
    const int newN = ORDER - 2 - mid;
    node->n    = mid;
    newNode->n = newN;

    // metade direita das chaves -> newNode
    for (int j = 0; j < newN; j++)
        newNode->keys[j] = node->keys[j + mid + 1];

    // metade direita dos filhos -> newNode (so indices, sem carregar nos)
    if (!node->isLeaf()) {
        for (int j = 0; j <= newN; j++) {
            newNode->filhoIdx[j] = node->filhoIdx[j + mid + 1];
            node->filhoIdx[j + mid + 1] = 0;
        }
    }

    // abre espaco e insere o ponteiro para newNode no pai (posicao i+1)
    for (int j = pai->n; j >= i + 1; j--)
        pai->filhoIdx[j + 1] = pai->filhoIdx[j];
    pai->filhoIdx[i + 1] = newNodeIdx;

    // promove a chave do meio para o pai (posicao i)
    for (int j = pai->n - 1; j >= i; j--)
        pai->keys[j + 1] = pai->keys[j];
    pai->keys[i] = node->keys[mid];
    pai->n++;

    pool_.markDirty(node);
    pool_.markDirty(newNode);
    pool_.markDirty(pai);

    pool_.unpin(nodeIdx);
    pool_.unpin(paiIdx);
    pool_.unpin(newNodeIdx);
}

// ---------------------------------------------------------------------------
// INSERCAO REATIVA (bottom-up / convencional)
//
// Desce ate a folha SEM dividir nada. Insere a chave. Se o no transbordar
// (passar a ter ORDER chaves), divide-o em torno de mid = ORDER/2, promovendo
// a chave do meio. A promocao sobe pela pilha de recursao: cada ancestral
// recebe (upKey, rightIdx); se ele tambem transbordar, divide e promove de
// novo. A raiz e' tratada pelo chamador (insert), que cresce a arvore.
//
// Particao de um no com ORDER chaves (mid = ORDER/2):
//   esquerdo  -> ORDER/2 chaves          (>= ocupacao minima p/ todo m>=3)
//   promovida -> chave em mid
//   direito   -> ceil(ORDER/2)-1 chaves  (>= ocupacao minima p/ todo m>=3)
// Diferente do split preemptivo, isto NUNCA gera no com 0 chaves: vale p/ m=3.
//
// Pinagem: o pai permanece pinado durante a recursao no filho, para que seu
// ponteiro continue valido ao inserir a chave promovida no retorno. O pool
// tolera o "soft overflow" temporario de manter o caminho (altura) pinado.
// ---------------------------------------------------------------------------
template <typename KeyType, int ORDER>
typename BTree<KeyType, ORDER>::InsertResult
BTree<KeyType, ORDER>::insertReactive(int nodeIdx, KeyType key) {
    pool_.pin(nodeIdx);
    Node* node = pool_.fetch(nodeIdx);

    // ---------------- FOLHA ----------------
    if (node->isLeaf()) {
        int pos = node->n;
        while (pos > 0 && key < node->keys[pos - 1]) pos--;

        if (node->n < ORDER - 1) {                 // cabe: insercao simples
            for (int j = node->n; j > pos; j--) node->keys[j] = node->keys[j - 1];
            node->keys[pos] = key;
            node->n++;
            pool_.markDirty(node);
            pool_.unpin(nodeIdx);
            return {false, KeyType{}, 0};
        }

        // Transbordo: monta ORDER chaves num buffer local e divide.
        KeyType tmp[ORDER];
        for (int j = 0; j < pos; j++)            tmp[j]     = node->keys[j];
        tmp[pos] = key;
        for (int j = pos; j < node->n; j++)      tmp[j + 1] = node->keys[j];

        const int mid = ORDER / 2;               // chave promovida
        const int rn  = (ORDER - 1) - mid;       // chaves no no direito

        pool_.pin(nodeIdx);                      // protege antes de alocar
        Node* right = allocateNode();
        int rightIdx = right->diskIdx;
        pool_.pin(rightIdx);
        node  = pool_.fetch(nodeIdx);            // re-fetch (pinado: cache hit)
        right = pool_.fetch(rightIdx);

        node->n = mid;
        for (int j = 0; j < mid; j++) node->keys[j] = tmp[j];
        right->n = rn;
        for (int j = 0; j < rn; j++) right->keys[j] = tmp[mid + 1 + j];

        pool_.markDirty(node);
        pool_.markDirty(right);
        KeyType up = tmp[mid];
        pool_.unpin(rightIdx);
        pool_.unpin(nodeIdx);                    // do pin extra acima
        pool_.unpin(nodeIdx);                    // do pin de entrada
        return {true, up, rightIdx};
    }

    // ---------------- NO INTERNO ----------------
    // Localiza o filho que recebe a chave (mesmo criterio do insertNonFull).
    int i = node->n - 1;
    while (i >= 0 && key < node->keys[i]) i--;
    i++;
    int childIdx = node->filhoIdx[i];

    // Recursao com o pai ainda pinado (precisamos dele de volta no retorno).
    InsertResult r = insertReactive(childIdx, key);
    node = pool_.fetch(nodeIdx);                 // pinado: cache hit

    if (!r.promoted) {                           // filho absorveu sem dividir
        pool_.unpin(nodeIdx);
        return {false, KeyType{}, 0};
    }

    // O filho dividiu: insere (upKey em i, rightIdx em i+1) neste no.
    if (node->n < ORDER - 1) {                   // cabe aqui: para a propagacao
        for (int j = node->n;     j > i;     j--) node->keys[j]     = node->keys[j - 1];
        for (int j = node->n + 1; j > i + 1; j--) node->filhoIdx[j] = node->filhoIdx[j - 1];
        node->keys[i]         = r.upKey;
        node->filhoIdx[i + 1] = r.rightIdx;
        node->n++;
        pool_.markDirty(node);
        pool_.unpin(nodeIdx);
        return {false, KeyType{}, 0};
    }

    // Este no tambem transborda: monta ORDER chaves / ORDER+1 filhos e divide.
    KeyType tmpKeys[ORDER];
    int     tmpCh[ORDER + 1];
    for (int j = 0; j < i; j++)            tmpKeys[j]     = node->keys[j];
    tmpKeys[i] = r.upKey;
    for (int j = i; j < node->n; j++)      tmpKeys[j + 1] = node->keys[j];
    for (int j = 0; j <= i; j++)           tmpCh[j]       = node->filhoIdx[j];
    tmpCh[i + 1] = r.rightIdx;
    for (int j = i + 1; j <= node->n; j++) tmpCh[j + 1]   = node->filhoIdx[j];

    const int mid = ORDER / 2;
    const int rn  = (ORDER - 1) - mid;

    pool_.pin(nodeIdx);
    Node* right = allocateNode();
    int rightIdx = right->diskIdx;
    pool_.pin(rightIdx);
    node  = pool_.fetch(nodeIdx);
    right = pool_.fetch(rightIdx);

    node->n = mid;
    for (int j = 0; j < mid; j++)  node->keys[j]     = tmpKeys[j];
    for (int j = 0; j <= mid; j++) node->filhoIdx[j] = tmpCh[j];
    for (int j = mid + 1; j < ORDER; j++) node->filhoIdx[j] = 0;  // limpa cauda

    right->n = rn;
    for (int j = 0; j < rn; j++)  right->keys[j]     = tmpKeys[mid + 1 + j];
    for (int j = 0; j <= rn; j++) right->filhoIdx[j] = tmpCh[mid + 1 + j];

    pool_.markDirty(node);
    pool_.markDirty(right);
    KeyType up = tmpKeys[mid];
    pool_.unpin(rightIdx);
    pool_.unpin(nodeIdx);                        // do pin extra acima
    pool_.unpin(nodeIdx);                        // do pin de entrada
    return {true, up, rightIdx};
}

// ===========================================================================
// SEARCH
// ===========================================================================
template <typename KeyType, int ORDER>
SearchResult<KeyType, ORDER> BTree<KeyType, ORDER>::search(KeyType key) {
    
    if (this->rootIdx_ == 0){
      return {0, 0, false};  
    } 

    int currentIdx = this->rootIdx_;
    int lastNodeIdx = 0;  // ultimo no visitado (a folha onde a busca para)
    int insertPos   = 0;  // posicao de insercao da chave dentro desse no
    while (currentIdx != 0) {

        this->pool_.pin(currentIdx);
        Node* node = this->pool_.fetch(currentIdx);

        int i = 0;
        while (i < node->n && key >= node->keys[i]) {
    
            if (key == node->keys[i]) {
                this->pool_.unpin(currentIdx);
                return {currentIdx, i, true};
            }
            i++;
    
        }
    
        lastNodeIdx = currentIdx;
        insertPos = i;
        int nextChildIdx = node->filhoIdx[i];
        this->pool_.unpin(currentIdx);
        currentIdx = nextChildIdx;   // 0 numa folha -> encerra o laco
    
    }
    // Nao encontrada: (lastNodeIdx, insertPos) e' o ponto de insercao na folha.
    return {lastNodeIdx, insertPos, false};
}


// ===========================================================================
// REMOCAO  (estilo CLRS)
//
// Estrategia geral: descer recursivamente garantindo que o no em que vamos
// entrar tenha SEMPRE mais que kMinKeys chaves (logo, pode perder uma sem
// violar a ocupacao minima). Quando um filho esta no minimo, antes de descer
// nele fazemos um emprestimo (rotacao) de um irmao ou uma fusao com um irmao.
// ===========================================================================
template <typename KeyType, int ORDER>
bool BTree<KeyType, ORDER>::remove(KeyType key) {
    if (rootIdx_ == 0) return false;

    bool existed = (delPolicy_ == DeletePolicy::Reactive)
                       ? removeReactive(rootIdx_, key)
                       : removeFromSubtree(rootIdx_, key);

    // Reducao de altura: se a raiz esvaziou, seu unico filho assume e o slot
    // antigo da raiz e' reciclado pela free-list.
    int oldRoot = rootIdx_;
    pool_.pin(oldRoot);
    Node* root = pool_.fetch(oldRoot);
    bool collapse = (root->n == 0);
    int  newRoot  = collapse ? (root->isLeaf() ? 0 : root->filhoIdx[0]) : rootIdx_;
    pool_.unpin(oldRoot);
    if (collapse) {
        freeNode(oldRoot);
        rootIdx_ = newRoot;
    }
    return existed;
}

// Remove 'key' da subarvore enraizada em nodeIdx. Retorna se a chave existia.
// Invariante de entrada: nodeIdx tem > kMinKeys chaves OU e' a raiz.
template <typename KeyType, int ORDER>
bool BTree<KeyType, ORDER>::removeFromSubtree(int nodeIdx, KeyType key) {
    pool_.pin(nodeIdx);
    Node* node = pool_.fetch(nodeIdx);

    // Primeira posicao i com keys[i] >= key.
    int i = 0;
    while (i < node->n && key > node->keys[i]) i++;
    bool here = (i < node->n && node->keys[i] == key);

    if (here) {
        if (node->isLeaf()) {                 // CASO 1: chave numa folha
            removeFromLeaf(node, i);
            pool_.markDirty(node);
            pool_.unpin(nodeIdx);
            return true;
        }
        pool_.unpin(nodeIdx);                 // CASO 2: chave num no interno
        removeFromInternal(nodeIdx, i);
        return true;
    }

    if (node->isLeaf()) {                     // chave nao existe na arvore
        pool_.unpin(nodeIdx);
        return false;
    }

    // CASO 3: a chave (se existir) esta no filho i. Garante que ele pode
    // perder uma chave antes de descer.
    bool last = (i == node->n);
    int childIdx = node->filhoIdx[i];

    pool_.pin(childIdx);
    bool deficient = (pool_.fetch(childIdx)->n <= kMinKeys);
    pool_.unpin(childIdx);

    if (deficient) {
        fillChild(nodeIdx, i);
        node = pool_.fetch(nodeIdx);
        // Se o filho i era o ultimo e foi fundido com o anterior, ele agora
        // vive no indice i-1.
        childIdx = (last && i > node->n) ? node->filhoIdx[i - 1]
                                         : node->filhoIdx[i];
    }
    pool_.unpin(nodeIdx);
    return removeFromSubtree(childIdx, key);
}

// CASO 1: remove a chave i de uma folha (apenas desloca as seguintes).
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::removeFromLeaf(Node* node, int i) {
    for (int j = i + 1; j < node->n; j++) node->keys[j - 1] = node->keys[j];
    node->n--;
}

// CASO 2: remove a chave i de um no interno nodeIdx.
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::removeFromInternal(int nodeIdx, int i) {
    pool_.pin(nodeIdx);
    Node* node = pool_.fetch(nodeIdx);
    KeyType k   = node->keys[i];
    int leftIdx  = node->filhoIdx[i];
    int rightIdx = node->filhoIdx[i + 1];

    // 2a: filho esquerdo folgado -> substitui por predecessor e remove-o la.
    pool_.pin(leftIdx);
    int leftN = pool_.fetch(leftIdx)->n;
    pool_.unpin(leftIdx);
    if (leftN > kMinKeys) {
        KeyType pred = getPredecessor(leftIdx);
        node = pool_.fetch(nodeIdx);
        node->keys[i] = pred;
        pool_.markDirty(node);
        pool_.unpin(nodeIdx);
        removeFromSubtree(leftIdx, pred);
        return;
    }

    // 2b: filho direito folgado -> substitui por sucessor e remove-o la.
    pool_.pin(rightIdx);
    int rightN = pool_.fetch(rightIdx)->n;
    pool_.unpin(rightIdx);
    if (rightN > kMinKeys) {
        KeyType succ = getSuccessor(rightIdx);
        node = pool_.fetch(nodeIdx);
        node->keys[i] = succ;
        pool_.markDirty(node);
        pool_.unpin(nodeIdx);
        removeFromSubtree(rightIdx, succ);
        return;
    }

    // 2c: ambos no minimo -> funde k + filho direito no esquerdo; remove k la.
    pool_.unpin(nodeIdx);
    mergeChildren(nodeIdx, i);
    removeFromSubtree(leftIdx, k);
}

// Maior chave da subarvore childIdx (desce sempre pelo ultimo filho).
template <typename KeyType, int ORDER>
KeyType BTree<KeyType, ORDER>::getPredecessor(int childIdx) {
    int idx = childIdx;
    pool_.pin(idx);
    Node* node = pool_.fetch(idx);
    while (!node->isLeaf()) {
        int next = node->filhoIdx[node->n];
        pool_.unpin(idx);
        idx = next;
        pool_.pin(idx);
        node = pool_.fetch(idx);
    }
    KeyType k = node->keys[node->n - 1];
    pool_.unpin(idx);
    return k;
}

// Menor chave da subarvore childIdx (desce sempre pelo primeiro filho).
template <typename KeyType, int ORDER>
KeyType BTree<KeyType, ORDER>::getSuccessor(int childIdx) {
    int idx = childIdx;
    pool_.pin(idx);
    Node* node = pool_.fetch(idx);
    while (!node->isLeaf()) {
        int next = node->filhoIdx[0];
        pool_.unpin(idx);
        idx = next;
        pool_.pin(idx);
        node = pool_.fetch(idx);
    }
    KeyType k = node->keys[0];
    pool_.unpin(idx);
    return k;
}

// CASO 3 (pre-descida): garante que o filho i tenha > kMinKeys chaves,
// emprestando de um irmao ou fundindo com ele.
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::fillChild(int paiIdx, int i) {
    pool_.pin(paiIdx);
    Node* pai = pool_.fetch(paiIdx);

    // Empresta do irmao esquerdo, se ele tiver folga.
    if (i > 0) {
        int leftSib = pai->filhoIdx[i - 1];
        pool_.pin(leftSib);
        int sibN = pool_.fetch(leftSib)->n;
        pool_.unpin(leftSib);
        if (sibN > kMinKeys) {
            borrowFromPrev(paiIdx, i);
            pool_.unpin(paiIdx);
            return;
        }
    }

    // Empresta do irmao direito, se ele tiver folga.
    pai = pool_.fetch(paiIdx);
    if (i < pai->n) {
        int rightSib = pai->filhoIdx[i + 1];
        pool_.pin(rightSib);
        int sibN = pool_.fetch(rightSib)->n;
        pool_.unpin(rightSib);
        if (sibN > kMinKeys) {
            borrowFromNext(paiIdx, i);
            pool_.unpin(paiIdx);
            return;
        }
    }

    // Sem irmao folgado: funde. Se i e' o ultimo filho, funde com o anterior.
    pai = pool_.fetch(paiIdx);
    int mergeAt = (i < pai->n) ? i : i - 1;
    pool_.unpin(paiIdx);
    mergeChildren(paiIdx, mergeAt);
}

// Rotacao: o filho i recebe uma chave vinda do irmao esquerdo (i-1),
// passando pelo separador no pai.
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::borrowFromPrev(int paiIdx, int i) {
    pool_.pin(paiIdx);
    Node* pai = pool_.fetch(paiIdx);
    int childIdx = pai->filhoIdx[i];
    int sibIdx   = pai->filhoIdx[i - 1];

    pool_.pin(childIdx);
    pool_.pin(sibIdx);
    Node* child = pool_.fetch(childIdx);
    Node* sib   = pool_.fetch(sibIdx);
    pai = pool_.fetch(paiIdx);

    bool internal = !child->isLeaf();

    // Abre espaco no inicio do filho.
    for (int j = child->n - 1; j >= 0; j--) child->keys[j + 1] = child->keys[j];
    if (internal)
        for (int j = child->n; j >= 0; j--) child->filhoIdx[j + 1] = child->filhoIdx[j];

    // Separador do pai desce para o inicio do filho.
    child->keys[0] = pai->keys[i - 1];
    if (internal) {
        child->filhoIdx[0] = sib->filhoIdx[sib->n];
        sib->filhoIdx[sib->n] = 0;
    }
    // Ultima chave do irmao sobe para o pai.
    pai->keys[i - 1] = sib->keys[sib->n - 1];

    child->n++;
    sib->n--;

    pool_.markDirty(child);
    pool_.markDirty(sib);
    pool_.markDirty(pai);
    pool_.unpin(childIdx);
    pool_.unpin(sibIdx);
    pool_.unpin(paiIdx);
}

// Rotacao: o filho i recebe uma chave vinda do irmao direito (i+1),
// passando pelo separador no pai.
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::borrowFromNext(int paiIdx, int i) {
    pool_.pin(paiIdx);
    Node* pai = pool_.fetch(paiIdx);
    int childIdx = pai->filhoIdx[i];
    int sibIdx   = pai->filhoIdx[i + 1];

    pool_.pin(childIdx);
    pool_.pin(sibIdx);
    Node* child = pool_.fetch(childIdx);
    Node* sib   = pool_.fetch(sibIdx);
    pai = pool_.fetch(paiIdx);

    bool internal = !child->isLeaf();

    // Separador do pai desce para o fim do filho.
    child->keys[child->n] = pai->keys[i];
    if (internal) child->filhoIdx[child->n + 1] = sib->filhoIdx[0];

    // Primeira chave do irmao sobe para o pai.
    pai->keys[i] = sib->keys[0];

    // Desloca o irmao para a esquerda.
    for (int j = 1; j < sib->n; j++) sib->keys[j - 1] = sib->keys[j];
    if (internal) {
        for (int j = 1; j <= sib->n; j++) sib->filhoIdx[j - 1] = sib->filhoIdx[j];
        sib->filhoIdx[sib->n] = 0;
    }

    child->n++;
    sib->n--;

    pool_.markDirty(child);
    pool_.markDirty(sib);
    pool_.markDirty(pai);
    pool_.unpin(childIdx);
    pool_.unpin(sibIdx);
    pool_.unpin(paiIdx);
}

// Funde o filho i+1 dentro do filho i, puxando para baixo o separador (chave i)
// do pai. Apos: o pai perde a chave i e o ponteiro i+1; o filho i passa a ter
// (n_esq + 1 + n_dir) chaves. O slot do filho direito e' reciclado (free-list).
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::mergeChildren(int paiIdx, int i) {
    pool_.pin(paiIdx);
    Node* pai = pool_.fetch(paiIdx);
    int leftIdx  = pai->filhoIdx[i];
    int rightIdx = pai->filhoIdx[i + 1];

    pool_.pin(leftIdx);
    pool_.pin(rightIdx);
    Node* left  = pool_.fetch(leftIdx);
    Node* right = pool_.fetch(rightIdx);
    pai = pool_.fetch(paiIdx);

    int  l = left->n;
    bool internal = !left->isLeaf();

    // Separador do pai desce para o fim do left.
    left->keys[l] = pai->keys[i];
    // Chaves do right.
    for (int j = 0; j < right->n; j++) left->keys[l + 1 + j] = right->keys[j];
    // Filhos do right.
    if (internal)
        for (int j = 0; j <= right->n; j++) left->filhoIdx[l + 1 + j] = right->filhoIdx[j];
    left->n = l + 1 + right->n;
    pool_.markDirty(left);

    // Remove a chave i e o ponteiro i+1 do pai.
    for (int j = i; j < pai->n - 1; j++) pai->keys[j] = pai->keys[j + 1];
    for (int j = i + 1; j < pai->n; j++) pai->filhoIdx[j] = pai->filhoIdx[j + 1];
    pai->filhoIdx[pai->n] = 0;
    pai->n--;
    pool_.markDirty(pai);

    pool_.unpin(leftIdx);
    pool_.unpin(rightIdx);
    pool_.unpin(paiIdx);

    // O no direito foi absorvido: recicla seu slot fisico.
    freeNode(rightIdx);
}

// ===========================================================================
// REMOCAO REATIVA (bottom-up / convencional)
//
// Espelho da insercao reativa. Desce ate a folha SEM rebalancear no caminho;
// remove a chave la; e, na VOLTA da recursao, se o filho visitado ficou com
// menos que kMinKeysReactive chaves, conserta o underflow (emprestimo de um
// irmao com folga, ou fusao). A fusao combina um no deficiente (kMin-1) com um
// irmao no minimo (kMin) => 2*kMin <= ORDER-1 para todo ORDER, inclusive
// ORDER=3. Por isso esta familia vale para qualquer m >= 3.
//
// Chave em no INTERNO: troca-se pela maior chave da subarvore esquerda
// (predecessor) e remove-se o predecessor recursivamente — toda remocao fisica
// acontece numa folha, como na arvore-B classica.
// ===========================================================================
template <typename KeyType, int ORDER>
bool BTree<KeyType, ORDER>::removeReactive(int nodeIdx, KeyType key) {
    pool_.pin(nodeIdx);
    Node* node = pool_.fetch(nodeIdx);

    int i = 0;
    while (i < node->n && key > node->keys[i]) i++;
    bool here = (i < node->n && node->keys[i] == key);

    if (here && node->isLeaf()) {            // CASO 1: chave numa folha
        removeFromLeaf(node, i);
        pool_.markDirty(node);
        pool_.unpin(nodeIdx);
        return true;
    }

    if (here) {                              // CASO 2: chave num no interno
        int leftChild = node->filhoIdx[i];
        pool_.unpin(nodeIdx);
        KeyType pred = getPredecessor(leftChild);
        node = pool_.fetch(nodeIdx);
        pool_.pin(nodeIdx);
        node->keys[i] = pred;
        pool_.markDirty(node);
        pool_.unpin(nodeIdx);
        removeReactive(leftChild, pred);     // remove o predecessor na folha
        fixUnderflow(nodeIdx, i);            // conserta o filho i na volta
        return true;
    }

    if (node->isLeaf()) {                    // chave nao existe na arvore
        pool_.unpin(nodeIdx);
        return false;
    }

    // CASO 3: desce no filho i; conserta o underflow dele na volta.
    int childIdx = node->filhoIdx[i];
    int childPos = i;
    pool_.unpin(nodeIdx);
    bool existed = removeReactive(childIdx, key);
    if (existed) fixUnderflow(nodeIdx, childPos);
    return existed;
}

// Conserta o filho i de paiIdx caso ele esteja abaixo do minimo
// (kMinKeysReactive). Emprestimo de um irmao com folga, ou fusao. A fusao pode
// deixar o pai abaixo do minimo, o que sera consertado pelo chamador acima.
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::fixUnderflow(int paiIdx, int i) {
    pool_.pin(paiIdx);
    Node* pai = pool_.fetch(paiIdx);
    int childIdx = pai->filhoIdx[i];

    pool_.pin(childIdx);
    int childN = pool_.fetch(childIdx)->n;
    pool_.unpin(childIdx);

    if (childN >= kMinKeysReactive) {        // sem underflow: nada a fazer
        pool_.unpin(paiIdx);
        return;
    }

    // Empresta do irmao esquerdo, se tiver folga (> kMinKeysReactive).
    if (i > 0) {
        int leftSib = pai->filhoIdx[i - 1];
        pool_.pin(leftSib);
        int sibN = pool_.fetch(leftSib)->n;
        pool_.unpin(leftSib);
        if (sibN > kMinKeysReactive) {
            borrowFromPrev(paiIdx, i);
            pool_.unpin(paiIdx);
            return;
        }
    }

    // Empresta do irmao direito, se tiver folga.
    pai = pool_.fetch(paiIdx);
    if (i < pai->n) {
        int rightSib = pai->filhoIdx[i + 1];
        pool_.pin(rightSib);
        int sibN = pool_.fetch(rightSib)->n;
        pool_.unpin(rightSib);
        if (sibN > kMinKeysReactive) {
            borrowFromNext(paiIdx, i);
            pool_.unpin(paiIdx);
            return;
        }
    }

    // Sem irmao folgado: funde. Se i e' o ultimo filho, funde com o anterior.
    pai = pool_.fetch(paiIdx);
    int mergeAt = (i < pai->n) ? i : i - 1;
    pool_.unpin(paiIdx);
    mergeChildren(paiIdx, mergeAt);
}

// ===========================================================================
// IMPRESSAO (depuracao)
// ===========================================================================
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::print(int maxLevel) {
    if (rootIdx_ == 0) {
        std::cout << "(arvore vazia)\n";
        return;
    }
    printNodeIdx(rootIdx_, 0, maxLevel);
}

template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::printNodeIdx(int diskIdx, int level, int maxLevel) {
    if (diskIdx == 0) return;
    if (maxLevel >= 0 && level > maxLevel) return;

    pool_.pin(diskIdx);
    Node* node = pool_.fetch(diskIdx);

    for (int s = 0; s < level; s++) std::cout << "  ";
    std::cout << "[N" << diskIdx << "] ";
    for (int k = 0; k < node->n; k++) {
        std::cout << node->keys[k];
        if (k < node->n - 1) std::cout << " ";
    }
    std::cout << "\n";

    // Snapshot dos filhos antes de recursar, para liberar o pai.
    bool isInternal = !node->isLeaf();
    int childCount = isInternal ? node->n + 1 : 0;
    int childIdxs[ORDER];
    for (int k = 0; k < childCount; k++) childIdxs[k] = node->filhoIdx[k];

    pool_.unpin(diskIdx);
    for (int k = 0; k < childCount; k++)
        printNodeIdx(childIdxs[k], level + 1, maxLevel);
}

#endif // BTREE_TPP