#ifndef BTREE_H
#define BTREE_H

#include <string>
#include <vector>
#include "node.h"
#include "disk_manager.h"
#include "buffer_pool.h"

// ============================================================================
//  btree.h  —  BTree: o algoritmo da arvore-B, e SO o algoritmo.
//
//  Esta classe nao toca o disco diretamente nem gerencia cache. Ela:
//    * possui um DiskIO (camada de arquivo) e um BufferPool (camada de cache),
//      construidos nesta ordem;
//    * mantem o indice da raiz (rootIdx) e o persiste em "<arquivo>.meta";
//    * implementa insert / search / remove / print pedindo nos ao BufferPool.
//
//  Os contadores de acesso a disco sao do DiskIO; aqui apenas os reexpomos
//  por conveniencia (diskReads/diskWrites).
//
//  Parametros de template:
//    KeyType  tipo da chave (precisa de operadores < , > , ==).
//    ORDER    ordem da arvore = numero maximo de filhos por no.
//             => maximo de chaves por no  = ORDER - 1
//             => minimo de chaves por no:
//                  familia REATIVA   : ceil(ORDER/2) - 1 = (ORDER-1)/2  (padrao)
//                  familia PREEMPTIVA : (ORDER-2)/2  (relaxado p/ ordens impares)
//             A familia reativa vale para todo ORDER >= 3; a preemptiva
//             degenera em ORDER=3 (ver abaixo).
//
//  Persistencia: o arquivo de dados ".dat" guarda os nos; o ".meta" guarda o
//  rootIdx E a free-list de slots reciclaveis. Ao abrir um arquivo existente,
//  a arvore (e a free-list) e' recuperada.
//
//  REAPROVEITAMENTO DE NOS (free-list): quando uma fusao descarta um no, ou
//  quando a raiz esvazia e e' colapsada, o slot fisico correspondente nao e'
//  abandonado: seu indice vai para uma free-list (uma pilha de inteiros em
//  RAM, persistida no .meta). A proxima alocacao reaproveita esse slot em vez
//  de crescer o arquivo. Pode ser desligado (reuseNodes=false) para medir a
//  ocupacao do arquivo COM x SEM reuso.
//
//  DUAS FAMILIAS DE ALGORITMO (split na insercao + rebalanceamento na remocao
//  sao escolhas PAREADAS, cada uma selecionavel em tempo de execucao):
//
//   * PREEMPTIVA (top-down, single-pass, estilo CLRS)
//       Insercao: ao descer, divide qualquer no CHEIO no caminho ANTES de
//         entrar nele (a folha-destino sempre tem espaco; split nunca volta).
//       Remocao: ao descer, se o filho-alvo esta no minimo, faz emprestimo ou
//         fusao ANTES de entrar (nunca precisa voltar para consertar).
//       Vantagem: uma unica passada, nunca revisita o pai.
//       Ocupacao minima usada: (ORDER-2)/2. Para ORDER IMPAR esse minimo cai
//         abaixo do padrao; no caso extremo ORDER=3 vira 0, e tanto o split
//         (gera no com 0 chaves) quanto a fusao (admite no de 0 chaves) levam
//         a uma estrutura degenerada. => NAO vale para m=3.
//
//   * REATIVA (bottom-up, two-pass, "convencional")
//       Insercao: desce ate a folha sem dividir nada, insere e, se o no
//         transbordar (ORDER chaves), divide e PROMOVE a chave do meio ao pai,
//         propagando o split para cima.
//       Remocao: desce ate a folha (trocando chave interna por seu
//         predecessor), remove e, se o no ficar abaixo do minimo, conserta o
//         underflow (emprestimo/fusao) na VOLTA, propagando para cima.
//       Ocupacao minima usada: ceil(ORDER/2)-1 = (ORDER-1)/2 (o minimo padrao
//         de arvore-B). A fusao combina um no deficiente (min-1) com um irmao
//         no minimo: 2*min <= ORDER-1 para todo ORDER. => vale para TODO m>=3,
//         inclusive m=3.
//
//  Por que m=3 so funciona na familia reativa: a divisao/fusao preemptiva e'
//  proativa (age em nos no minimo), exigindo 2*min+1 <= ORDER-1, ou seja
//  min <= (ORDER-2)/2 — que zera em ORDER=3. A reativa e' "sob demanda" (age
//  em nos ja deficientes), exigindo apenas 2*min <= ORDER-1, satisfeito pelo
//  minimo padrao mesmo em ORDER=3. Insert e delete sao, portanto, duas faces
//  da mesma escolha: a insercao preemptiva e' o que quebra m=3 na insercao,
//  assim como a remocao preemptiva e' o que quebra m=3 na remocao.
// ============================================================================

// Estrategia de divisao de nos na insercao (ver descricao acima).
enum class SplitPolicy { Preemptive, Reactive };

// Estrategia de rebalanceamento na remocao (par da SplitPolicy, ver acima).
enum class DeletePolicy { Preemptive, Reactive };

template <typename KeyType, int ORDER>
class BTree {
public:
    // Abre (ou cria) a arvore em 'filename' (.dat) + 'filename'.meta.
    // maxCacheSize = capacidade do buffer pool em nos.
    // policy    = estrategia de split na insercao (padrao Reactive).
    // delPolicy = estrategia de rebalanceamento na remocao (padrao Reactive).
    //   Os padroes (Reactive/Reactive) sao corretos para TODO m, inclusive
    //   m=3. Use Preemptive/Preemptive para a abordagem classica top-down
    //   (single-pass), valida apenas para m>=4. Misturar as familias so faz
    //   sentido para m>=4 (onde as ocupacoes minimas coincidem).
    // reuseNodes = liga/desliga o reaproveitamento de slots (free-list). O
    //   padrao e' true; passe false para medir a ocupacao do arquivo sem reuso.
    explicit BTree(const std::string& filename, int maxCacheSize = 3,
                   SplitPolicy policy = SplitPolicy::Reactive,
                   DeletePolicy delPolicy = DeletePolicy::Reactive,
                   bool reuseNodes = true);

    // Faz flush dos nos dirty e grava o rootIdx em .meta.
    ~BTree();

    // --- OPERACOES DA ARVORE ---

    // Insere 'key'. Chaves duplicadas sao permitidas pela mecanica de insercao
    // (vao para a folha apropriada); a aplicacao decide se isso e' desejado.
    void insert(KeyType key);

    // Procura 'key'. Ver SearchResult (node.h) para o significado dos campos.
    // Pre-condicao: a arvore pode estar vazia (retorna found=false).
    SearchResult<KeyType, ORDER> search(KeyType key);

    // Remove 'key'. Retorna true se a chave existia (e foi removida), false
    // caso contrario. Mantem as invariantes da arvore-B (ocupacao minima,
    // possivel reducao de altura quando a raiz esvazia).
    bool remove(KeyType key);

    // Imprime a arvore por niveis (depuracao). maxLevel<0 imprime tudo.
    void print(int maxLevel = -1);

    // --- CONTADORES DE DISCO (delegados ao DiskIO) ---
    int  diskReads()  const { return io_.reads();  }
    int  diskWrites() const { return io_.writes(); }
    void resetDiskCounters() { io_.resetCounters(); }

    // --- INSPECAO / CONTROLE DO BUFFER POOL (delegados ao BufferPool) ---
    int  cacheSize()    const { return pool_.size();    }
    int  maxCacheSize() const { return pool_.maxSize(); }
    void setMaxCacheSize(int n) { pool_.setMaxSize(n); }

    // Grava em disco todos os nos dirty (sem despeja-los do cache).
    void flush() { pool_.flush(); }

    // --- ESTRATEGIAS (para benchmarking comparativo) ---
    SplitPolicy splitPolicy()       const { return policy_; }
    void setSplitPolicy(SplitPolicy p)    { policy_ = p; }
    DeletePolicy deletePolicy()     const { return delPolicy_; }
    void setDeletePolicy(DeletePolicy p)  { delPolicy_ = p; }

    // --- REAPROVEITAMENTO DE NOS (free-list) ---
    bool reuseNodes()       const { return reuseNodes_; }
    void setReuseNodes(bool b)    { reuseNodes_ = b; }
    // Numero de slots fisicos ja alocados no arquivo (marca d'agua). Com reuso
    // ligado, tende a ser bem menor apos cargas com muitas remocoes.
    int  fileSlots()        const { return io_.nextFreeIdx() - 1; }
    // Slots atualmente livres aguardando reaproveitamento.
    int  freeSlots()        const { return static_cast<int>(freeList_.size()); }

private:
    using Node = BTreeNode<KeyType, ORDER>;

    // Ocupacao minima de um no nao-raiz.
    //   kMinKeys         : usado pela familia PREEMPTIVA (relaxado).
    //   kMinKeysReactive : usado pela familia REATIVA (minimo padrao de arvore-B).
    // Coincidem para ORDER par; diferem para ORDER impar (em ORDER=3, 0 vs 1).
    static constexpr int kMinKeys         = (ORDER - 2) / 2;
    static constexpr int kMinKeysReactive = (ORDER - 1) / 2;

    std::string             filename_;
    DiskIO<KeyType, ORDER>  io_;     // construido 1o (BufferPool depende dele)
    BufferPool<KeyType, ORDER> pool_;
    int                     rootIdx_;
    SplitPolicy             policy_;     // estrategia de split na insercao
    DeletePolicy            delPolicy_;  // estrategia de rebalanceamento na remocao
    bool                    reuseNodes_; // free-list ligada?
    std::vector<int>        freeList_;   // pilha de slots reciclaveis (LIFO)

    // --- free-list: alocacao/liberacao de slots fisicos ---
    Node* allocateNode();        // reaproveita um slot livre, ou cresce o arquivo
    void  freeNode(int diskIdx); // descarta do cache e (se reuso) recicla o slot

    // --- helpers de INSERCAO: PREEMPTIVA (top-down, estilo CLRS) ---
    void insertNonFull(int nodeIdx, KeyType key);
    void splitChild(int nodeIdx, int paiIdx, int i);

    // --- helpers de INSERCAO: REATIVA / CONVENCIONAL (bottom-up) ---
    // Resultado da insercao recursiva: se um split propagou para cima, devolve
    // a chave promovida (upKey) e o indice do novo no direito (rightIdx).
    struct InsertResult { bool promoted; KeyType upKey; int rightIdx; };
    InsertResult insertReactive(int nodeIdx, KeyType key);

    // --- helpers de REMOCAO: PREEMPTIVA (top-down, estilo CLRS) ---
    bool    removeFromSubtree(int nodeIdx, KeyType key);
    void    removeFromLeaf(Node* node, int i);
    void    removeFromInternal(int nodeIdx, int i);
    KeyType getPredecessor(int childIdx); // maior chave da subarvore childIdx
    KeyType getSuccessor(int childIdx);   // menor chave da subarvore childIdx
    void    fillChild(int paiIdx, int i); // garante > kMinKeys no filho i
    void    borrowFromPrev(int paiIdx, int i);
    void    borrowFromNext(int paiIdx, int i);
    void    mergeChildren(int paiIdx, int i); // funde filhos i e i+1 do pai

    // --- helpers de REMOCAO: REATIVA / CONVENCIONAL (bottom-up) ---
    // Remove na folha e conserta o underflow na volta da recursao. Reutiliza
    // borrowFromPrev/borrowFromNext/mergeChildren (operacoes estruturais puras),
    // mas com o limiar de ocupacao kMinKeysReactive.
    bool    removeReactive(int nodeIdx, KeyType key);
    void    fixUnderflow(int paiIdx, int i);  // conserta filho i se < kMinKeysReactive

    // --- impressao ---
    void printNodeIdx(int diskIdx, int level, int maxLevel);
};

#include "btree.tpp"

#endif // BTREE_H