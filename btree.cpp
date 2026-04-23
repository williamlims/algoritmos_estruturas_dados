#include <iostream>
#include "functions.h"

// Definição da estrutura de dados do nó da árvore
template <typename KeyType, int ORDER> // Define que o nó Node terá ordem m=ORDER 
class BTreeNode {
public:
    KeyType keys[ORDER - 1]; // Array estático que armazena as chaves do nó
    int n; // Quantas chaves temos nesse nó?
    BTreeNode* children[ORDER]; // Array de ponteiros que mapeiam subárvores 

    // Inicialização do nó com ponteiros nulos
    BTreeNode() : n(0) {
        for (int i = 0; i < ORDER; i++)
            children[i] = 0;
    }
};     