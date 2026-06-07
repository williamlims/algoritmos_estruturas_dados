#ifndef BTREE_H
#define BTREE_H

#include <string>
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
//             => minimo de chaves por no  = (ORDER - 2) / 2   (exceto a raiz)
//             Assume-se ORDER >= 4 (para ORDER=3 o split degenera).
//
//  Persistencia: o arquivo de dados ".dat" guarda os nos; o ".meta" guarda
//  apenas o rootIdx. Ao abrir um arquivo existente, a arvore e' recuperada.
// ============================================================================
template <typename KeyType, int ORDER>
class BTree {
public:
    // Abre (ou cria) a arvore em 'filename' (.dat) + 'filename'.meta.
    // maxCacheSize = capacidade do buffer pool em nos.
    explicit BTree(const std::string& filename, int maxCacheSize = 3);

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

private:
    using Node = BTreeNode<KeyType, ORDER>;

    // Ocupacao minima de um no nao-raiz. Para um no com este numero de chaves,
    // remover mais uma exige rebalanceamento (emprestimo ou fusao).
    static constexpr int kMinKeys = (ORDER - 2) / 2;

    std::string             filename_;
    DiskIO<KeyType, ORDER>  io_;     // construido 1o (BufferPool depende dele)
    BufferPool<KeyType, ORDER> pool_;
    int                     rootIdx_;

    // --- helpers de INSERCAO ---
    void insertNonFull(int nodeIdx, KeyType key);
    void splitChild(int nodeIdx, int paiIdx, int i);

    // --- helpers de REMOCAO (estilo CLRS) ---
    bool    removeFromSubtree(int nodeIdx, KeyType key);
    void    removeFromLeaf(Node* node, int i);
    void    removeFromInternal(int nodeIdx, int i);
    KeyType getPredecessor(int childIdx); // maior chave da subarvore childIdx
    KeyType getSuccessor(int childIdx);   // menor chave da subarvore childIdx
    void    fillChild(int paiIdx, int i); // garante > kMinKeys no filho i
    void    borrowFromPrev(int paiIdx, int i);
    void    borrowFromNext(int paiIdx, int i);
    void    mergeChildren(int paiIdx, int i); // funde filhos i e i+1 do pai

    // --- impressao ---
    void printNodeIdx(int diskIdx, int level, int maxLevel);
};

#include "btree.tpp"

#endif // BTREE_H