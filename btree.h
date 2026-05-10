#ifndef BTREE_H
#define BTREE_H

#include <iostream>

// Definição da estrutura de dados do nó da árvore
template <typename KeyType, int ORDER>
class BTreeNode {
public:
    KeyType keys[ORDER - 1];
    int n;
    BTreeNode* filho[ORDER];

    int filhoIdx[ORDER]; // índice do filho no disco
    int diskIdx; // índice do nó no disco
    bool dirty; // registra se nó foi alterado em memória principal e precisa ser alterado em disco

    // Construtor:
    BTreeNode() : n(0), diskIdx(0), dirty(true) {
        for (int i = 0; i < ORDER; i++) {
            filho[i] = nullptr;
            filhoIdx[i] = 0;
        }
    }
};

// Definição do nó em disco
template <typename KeyType, int ORDER>
struct NodeRecord {
    int n;
    KeyType keys[ORDER - 1];
    int filhoIdx[ORDER];   // índices, não ponteiros! 0 = nullptr
};

// Definição da estrutura de dados auxiliar de consulta
template <typename KeyType, int ORDER>
struct SearchResult {
    BTreeNode<KeyType, ORDER>* node;
    int keyIndex;
    bool found;
};

#endif
