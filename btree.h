#ifndef BTREE_H
#define BTREE_H

#include <iostream>

// Definição da estrutura de dados do nó da árvore
template <typename KeyType, int ORDER>
class BTreeNode {
public:
    KeyType keys[ORDER - 1];
    int n;
    BTreeNode* children[ORDER];

    BTreeNode() : n(0) {
        for (int i = 0; i < ORDER; i++)
            children[i] = nullptr;
    }
};

// B-Tree class declaration and implementation (templates must be in header)
template <typename KeyType, int ORDER>
class BTree {
private:
    BTreeNode<KeyType, ORDER>* root;

public:
    BTree() : root(nullptr) {}

    void Insert(KeyType key);
    void mSearch(KeyType key);
};

#endif
