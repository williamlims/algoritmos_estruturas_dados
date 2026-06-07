#ifndef BTREE_TPP
#define BTREE_TPP

#include <iostream>
#include <fstream>

// Implementacao de BTree. Incluido por btree.h.

// ===========================================================================
// CONSTRUCAO / DESTRUICAO / PERSISTENCIA DO META
// ===========================================================================
template <typename KeyType, int ORDER>
BTree<KeyType, ORDER>::BTree(const std::string& filename, int maxCacheSize)
    : filename_(filename),
      io_(filename),                 // abre/cria o .dat e calcula nextIdx
      pool_(io_, maxCacheSize),      // cache sobre o DiskIO
      rootIdx_(0) {
    // Recupera a raiz de uma arvore previamente persistida, se houver.
    std::ifstream meta(filename_ + ".meta");
    if (meta) meta >> rootIdx_;
}

template <typename KeyType, int ORDER>
BTree<KeyType, ORDER>::~BTree() {
    pool_.flush();                              // grava nos dirty
    std::ofstream meta(filename_ + ".meta");    // persiste a raiz
    meta << rootIdx_;
    // pool_ e io_ sao destruidos em seguida (flush final + close).
}

// ===========================================================================
// INSERCAO
// ===========================================================================
template <typename KeyType, int ORDER>
void BTree<KeyType, ORDER>::insert(KeyType key) {
    // CASO 1: arvore vazia -> cria a raiz.
    if (this->rootIdx_ == 0) {
        Node* newRoot = this->pool_.allocate();
        this->rootIdx_ = newRoot->diskIdx;
        newRoot->keys[0] = key;
        newRoot->n = 1;
        this->pool_.markDirty(newRoot);
        return;
    }

    // CASO 2: raiz cheia -> cresce a arvore (split da raiz).
    this->pool_.pin(this->rootIdx_);
    Node* root = this->pool_.fetch(this->rootIdx_);
    if (root->n == ORDER - 1) {
        int oldRootIdx = this->rootIdx_;

        Node* newRoot = this->pool_.allocate();
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
// Faz split preemptivo de qualquer filho cheio no caminho de descida.
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

    Node* newNode = pool_.allocate();
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

    bool existed = removeFromSubtree(rootIdx_, key);

    // Reducao de altura: se a raiz esvaziou, seu unico filho assume.
    int oldRoot = rootIdx_;
    pool_.pin(oldRoot);
    Node* root = pool_.fetch(oldRoot);
    if (root->n == 0) {
        rootIdx_ = root->isLeaf() ? 0 : root->filhoIdx[0];
        // O slot antigo da raiz fica orfao no disco (sem free-list).
    }
    pool_.unpin(oldRoot);
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
// (n_esq + 1 + n_dir) chaves. O slot do filho direito fica orfao em disco.
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
