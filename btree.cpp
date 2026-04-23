#include <iostream>
#include "functions.h"

// Definição da estrutura de dados do nó da árvore
template <typename KeyType, int ORDER> // Define que o nó Node terá ordem m=ORDER 
class BTreeNode {
public:
    // Array estático que armazena as chaves do nó
    KeyType keys[ORDER - 1];  
    
    // Quantas chaves temos nesse nó?
    int n;

    // Array de ponteiros que mapeiam subárvores 
    BTreeNode* children[ORDER]; 

    bool leaf; // flag

    // Inicialização do nó com ponteiros nulos
    BTreeNode(bool isLeaf = true) : n(0), leaf(isLeaf) {
        for (int i = 0; i < ORDER; i++)
            children[i] = nullptr;
    }
};     