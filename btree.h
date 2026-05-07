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

template <typename KeyType, int ORDER>
struct SearchResult {
    BTreeNode<KeyType, ORDER>* node;
    int keyIndex;
    bool found;
};

// Declaração da estrutura de dados B-Tree
template <typename KeyType, int ORDER>
class BTree {
    private: 
        // Inicializa raíz com nó vazio
        BTreeNode<KeyType, ORDER>* root = new BTreeNode<KeyType, ORDER>;

    public:
        // Construtor da árvore
        BTree(std::initializer_list<KeyType> chavesIniciais) {
            for (const KeyType& chave : chavesIniciais) Insert(chave);
        }


        // Seção de INSERÇÃO DE  REGISTRO ////////////////////////////////////
        void Insert(KeyType key){
            if (root->n == ORDER - 1) { // Se a raiz esta cheia
                BTreeNode<KeyType, ORDER>* oldRoot = root;
                BTreeNode<KeyType, ORDER>* newNode = new BTreeNode<KeyType, ORDER>;
                newNode->filho[0] = oldRoot;
                root = newNode;
                splitNode(oldRoot, newNode, 0);
                insertHelper(newNode, key);
            } else
                insertHelper(root, key);
        }
        

        void insertHelper(BTreeNode<KeyType, ORDER>* node, KeyType key){
            int i = node->n - 1; // Inicializa i para posição final do vetor de chaves

            // Verifica se o nó atual É folha
            // Caso seja folha, insere na posição correta deslocando os registros a direita
            if (node->filho[0] == nullptr){
                while (i >= 0 && key < node->keys[i]) {
                    node->keys[i + 1] = node->keys[i];
                    i--;
                }
                node->keys[i+1] = key;
                node->n = node->n + 1;
            } else { // Se não for folha, verifica qual caminho seguir na árvore para inserção
                while (i >= 0 && key < node->keys[i]) i--;
                i++;

                if (node->filho[i]->n == ORDER - 1) { // verifica se o filho está cheio
                    splitNode(node->filho[i], node, i); // envia filho correto para inserção para explodir
                    if (key > node->keys[i]) i++; 
                }

                // RECURSÃO
                // Envia o filho para insertHelper para inserir na nova folha filho[i]
                // Nós novos serão criados no split node
                insertHelper(node->filho[i], key); 
            }
        }

        // Splita o nó 
        void splitNode(BTreeNode<KeyType, ORDER>* node, BTreeNode<KeyType, ORDER>* pai, int i){ 
            BTreeNode<KeyType, ORDER>* newNode = new BTreeNode<KeyType, ORDER>;

            const int mid = (ORDER - 1) / 2;  
            node->n    = mid; 
            newNode->n = ORDER - 2 - mid;  

            for (int j = 0; j < newNode->n; j++) // Preenche novo nó até metade dos registros do nó explodido
                newNode->keys[j] = node->keys[j + mid + 1];

            // transfere filhos da metade direita do nó original pro novo nó
            // só faz sentido se node não é folha (tem filhos não-nulos)
            if (node->filho[0] != nullptr) {
                for (int j = 0; j <= newNode->n; j++) {
                    newNode->filho[j] = node->filho[j + mid + 1];
                    node->filho[j + mid + 1] = nullptr;  // limpa pra evitar dois donos
                }
            }
            
            // INSERINDO NÓ NOVO COMO FILHO
            for (int j = pai->n; j >= i + 1; j--) // desloca filhos do nó pai para direita para inserir novo filho
                pai->filho[j + 1] = pai->filho[j];

            // insere nó explodido como filho do mesmo pai 
            // na posição seguinte do nó atual(insere à direita)
            pai->filho[i + 1] = newNode; // 

            // INSERINDO CHAVE NA LISTA DE CHAVES DO NÓ PAI
            for (int j = pai->n - 1; j >= i; j--) // desloca chaves dos filhos para direita para inserir novo filho
                pai->keys[j + 1] = pai->keys[j];
            
            pai->keys[i] = node->keys[mid]; // Recebe chave que ficou de fora como chave do novo nó
            pai->n = pai->n + 1; // aumenta em 1 a contagem de filhos daquele nó

        }
        // Fim da seção INSERÇÃO DE REGISTRO ////////////////////////////////////

        // Procura registro
        SearchResult<KeyType, ORDER> mSearch(KeyType key) {
            BTreeNode<KeyType, ORDER>* node = root;
            BTreeNode<KeyType, ORDER>* pai = nullptr;
            int i = 0;

            while (node != nullptr) {
                i = 0;
                while (i < node->n && key >= node->keys[i]) {
                    if (key == node->keys[i]) return {node, i, true};
                    i++;
                }
                pai = node;
                node = node->filho[i];
            }

            return {pai, i, false};
        }

        

        void excluiRegistro(KeyType key){

        }

        // Seção de INTERAÇÃO COM REGISTROS ////////////////////////////////////
        void recoverSatellite(KeyType key){

        }

        // 
        void alterarRegistro(KeyType key){

        }

};

#endif
