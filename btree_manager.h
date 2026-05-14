#ifndef BTREE_MANAGER_H
#define BTREE_MANAGER_H

#include <string>
#include <unordered_map> 
#include <fstream>
#include "disk_manager.h"
#include "btree.h"

template <typename KeyType, int ORDER>
class BTreeManager {
public:

    BTreeManager(const std::string& filename): // Inicializa árvore .dat ou inicia .dat
        filename(filename), root(nullptr), rootIdx(0), nextIdx(1) {

        // Tenta carregar metadata de arvore existente
        std::ifstream meta(filename + ".meta");
        if (meta) {
            meta >> rootIdx;
            if (rootIdx > 0) {
                root = loadNode(rootIdx);   // lazy: so carrega o root
            }
            // Continua alocando a partir de onde paramos (baseado no tamanho do .dat)
            nextIdx = DiskIO<KeyType, ORDER>::allocateNewIdx(filename);
        } else {
            // Arvore nova: cria arquivo de dados vazio; nextIdx ja eh 1
            DiskIO<KeyType, ORDER>::createFile(filename);
        }
    }

    ~BTreeManager(){ // Executa flush e deleta árvore da memória principal
        flush();   // grava todos os dirty
    
        // Persiste metadata
        std::ofstream meta(filename + ".meta");
        meta << rootIdx;
        
        // Libera memoria de todo o cache
        for (auto& [idx, node] : cache) {
            delete node;
        }
    }

    // OPERAÇÕES B-TREE: ///////////////////////////////////////////////////////////////
    void Insert(KeyType key) {
        // CASO 1: Árvore vazia → cria a raiz
        if (root == nullptr) {
            root = allocateNode();
            rootIdx = root->diskIdx;     // persistência
            root->keys[0] = key;
            root->n = 1;
            markDirty(root);
            return;
        }
        
        // CASO 2: Raiz cheia → split de raiz
        if (root->n == ORDER - 1) {
            BTreeNode<KeyType, ORDER>* oldRoot = root;
            BTreeNode<KeyType, ORDER>* newRoot = allocateNode();
            setChild(newRoot, 0, oldRoot);   // sincroniza filho[] e filhoIdx[]
            
            root = newRoot;                  // atualiza ponteiro
            rootIdx = newRoot->diskIdx;      // atualiza o que vai pro .meta
            
            splitNode(oldRoot, newRoot, 0);
            insertHelper(newRoot, key);
            return;
        }
        
        // CASO 3: Raiz tem espaço → desce e insere
        insertHelper(root, key);
    }

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
            node = getChild(pai, i);   // ← única mudança
        }

        return {pai, i, false};
    }

    void deleteRegister(KeyType key){

    }

    void showRegister(KeyType key){

    }

private:
    std::string filename; // caminho do arquivo .dat onde a árvore é armazenada e persisitida
    std::unordered_map<int, BTreeNode<KeyType, ORDER>*> cache; // mapeia diskIdx → ponteiro do nó em RAM
    BTreeNode<KeyType, ORDER>* root;
    int rootIdx;   // diskIdx do root no arquivo
    int nextIdx;   // proximo diskIdx livre (counter stateful — nao depende do arquivo)

    // FUNÇÕES AUXILIARES ////////////////////////////////////////////////////////////////
    void insertHelper(BTreeNode<KeyType, ORDER>* node, KeyType key) {
        int i = node->n - 1;

        // É folha?
        if (node->filhoIdx[0] == 0) { // Verifica se tem filho na memória secundária
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i+1] = key;
            node->n = node->n + 1;
            markDirty(node);
        } else {
            while (i >= 0 && key < node->keys[i]) i--;
            i++;

            BTreeNode<KeyType, ORDER>* child = getChild(node, i);
            
            if (child->n == ORDER - 1) {
                splitNode(child, node, i);
                if (key > node->keys[i]) {
                    i++;
                    child = getChild(node, i);   // recaptura após split
                }
            }
            
            insertHelper(child, key);
        }
    }

    void splitNode(BTreeNode<KeyType, ORDER>* node, BTreeNode<KeyType, ORDER>* pai, int i) {
        BTreeNode<KeyType, ORDER>* newNode = allocateNode();

        const int mid = (ORDER - 1) / 2;
        node->n    = mid;
        newNode->n = ORDER - 2 - mid;

        // FASE 1: copia chaves da metade direita pro newNode
        for (int j = 0; j < newNode->n; j++)
            newNode->keys[j] = node->keys[j + mid + 1];

        // FASE 2: transfere filhos da metade direita (se node nao e folha)
        if (node->filhoIdx[0] != 0) {
            for (int j = 0; j <= newNode->n; j++) {
                BTreeNode<KeyType, ORDER>* child = getChild(node, j + mid + 1);
                setChild(newNode, j, child);
                setChild(node, j + mid + 1, nullptr);
            }
        }

        // FASE 3: desloca filhos do pai e insere newNode em i+1
        for (int j = pai->n; j >= i + 1; j--) {
            pai->filho[j + 1] = pai->filho[j];
            pai->filhoIdx[j + 1] = pai->filhoIdx[j];
        }
        setChild(pai, i + 1, newNode);

        // FASE 4: desloca chaves do pai e insere chave do meio
        for (int j = pai->n - 1; j >= i; j--)
            pai->keys[j + 1] = pai->keys[j];
        pai->keys[i] = node->keys[mid];
        pai->n = pai->n + 1;

        markDirty(node);
        markDirty(newNode);
        markDirty(pai);
    }

    // OPERAÇÕES DE BUFFER POOL: //////////////////////////////////////////////////////
    BTreeNode<KeyType, ORDER>* getChild(BTreeNode<KeyType, ORDER>* node, int i){
        // Caso 1: filho ja esta em RAM
        if (node->filho[i] != nullptr) return node->filho[i];
        
        // Caso 2: e' folha de verdade (sem filho)
        if (node->filhoIdx[i] == 0) return nullptr;
        
        // Caso 3: existe no disco mas nao em RAM, carrega
        BTreeNode<KeyType, ORDER>* child = loadNode(node->filhoIdx[i]);
        node->filho[i] = child;   // popula o ponteiro pra acessos futuros
        return child;
    }

    void setChild(BTreeNode<KeyType, ORDER>* parent, int i, BTreeNode<KeyType, ORDER>* child) {
        parent->filho[i] = child;
        parent->filhoIdx[i] = (child != nullptr) ? child->diskIdx : 0;
        markDirty(parent);
    }

    BTreeNode<KeyType, ORDER>* allocateNode(){
        BTreeNode<KeyType, ORDER>* node = new BTreeNode<KeyType, ORDER>();
        node->diskIdx = nextIdx++;
        cache[node->diskIdx] = node;
        return node;
    }

    void markDirty(BTreeNode<KeyType, ORDER>* node) { node->dirty = true; }

    void flush(){ // Salva toda estrutura da ram na memória secundária, mas não deleta da memória principal
        for (auto& [idx, node] : cache) {
            if (node->dirty) saveNode(node);
        }
    }
    
    void saveNode(BTreeNode<KeyType, ORDER>* node){ // converte nó e grava no disco
        NodeRecord<KeyType, ORDER> record = nodeToRecord(node);
        DiskIO<KeyType, ORDER>::writeRecord(filename, node->diskIdx, record);
        node->dirty = false;
    }   

    BTreeNode<KeyType, ORDER>* loadNode(int diskIdx){ // le do disco, adiciona ao cache
        // Cache hit: ja esta em RAM
        auto it = cache.find(diskIdx); // procura id no cache
        if (it != cache.end()) return it->second; // retorna cache hit

        // Cache miss: le do disco e salva no cache
        NodeRecord<KeyType, ORDER> record;
        DiskIO<KeyType, ORDER>::readRecord(filename, diskIdx, record);
        BTreeNode<KeyType, ORDER>* node = recordToNode(record, diskIdx);
        cache[diskIdx] = node; // salva disco no cache
        return node;
    } 

    void evict(BTreeNode<KeyType, ORDER>* node){ // evict do cache: remove da memória primária gravando se dirty = true
        if (node->dirty) saveNode(node);
        cache.erase(node->diskIdx);
        delete node;
    }    

    // MÉTODOS DE TRADUÇÃO ENTRE MEMÓRIA PRINCIPAL E SECUNDÁRIA ////////////////////////
    NodeRecord<KeyType, ORDER> nodeToRecord(BTreeNode<KeyType, ORDER>* node){
        NodeRecord<KeyType, ORDER> record;
        record.n = node->n;
        for (int i = 0; i < ORDER - 1; i++)
            record.keys[i] = node->keys[i];
        for (int i = 0; i < ORDER; i++)
            record.filhoIdx[i] = node->filhoIdx[i];
        return record;
    }

    BTreeNode<KeyType, ORDER>* recordToNode(const NodeRecord<KeyType, ORDER>& record, int diskIdx){
        BTreeNode<KeyType, ORDER>* node = new BTreeNode<KeyType, ORDER>();
        node->n = record.n;
        for (int i = 0; i < ORDER - 1; i++)
            node->keys[i] = record.keys[i];
        for (int i = 0; i < ORDER; i++)
            node->filhoIdx[i] = record.filhoIdx[i];
        node->diskIdx = diskIdx;
        node->dirty = false;          // veio diretamente do disco
        return node;
    }
};

#endif