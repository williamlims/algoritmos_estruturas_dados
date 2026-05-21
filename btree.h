#ifndef BTREE_H
#define BTREE_H

#include <iostream>

// Definição da estrutura de dados do nó da árvore
template <typename KeyType, int ORDER>
class BTreeNode {
public:
    KeyType keys[ORDER - 1];
    int n;

    int filhoIdx[ORDER]; // índice do filho no disco (0 = sem filho)
    int diskIdx;         // índice do nó no disco
    bool dirty;          // registra se nó foi alterado em memória principal e precisa ser gravado em disco

    BTreeNode() : n(0), diskIdx(0), dirty(true) {
        for (int i = 0; i < ORDER; i++) filhoIdx[i] = 0;
    }
};

// Definição do nó em disco
template <typename KeyType, int ORDER>
struct NodeRecord {
    int n;
    KeyType keys[ORDER - 1];
    int filhoIdx[ORDER];   // 0 = nullptr
};

// Resultado de uma busca. Guardamos diskIdx (estavel) em vez de ponteiro,
// que poderia ser invalidado por despejos posteriores no buffer pool.
template <typename KeyType, int ORDER>
struct SearchResult {
    int nodeIdx;
    int keyIndex;
    bool found;
};

#endif
