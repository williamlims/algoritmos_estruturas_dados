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
        filename(filename), root(nullptr), rootIdx(0) {

        // Tenta carregar metadata de arvore existente
        std::ifstream meta(filename + ".meta");
        if (meta) {
            meta >> rootIdx;
            if (rootIdx > 0) {
                root = loadNode(rootIdx);   // lazy: so carrega o root
            }
        } else {
            // Arvore nova: cria arquivo de dados vazio
            DiskIO<KeyType, ORDER>::createFile(filename);
        }
    }

    ~BTreeManager(){ // Executa flush e deleta da memória principal
        flush();   // grava todos os dirty
    
        // Persiste metadata
        std::ofstream meta(filename + ".meta");
        meta << rootIdx;
        
        // Libera memoria de todo o cache
        for (auto& [idx, node] : cache) {
            delete node;
        }
    }

    void Insert(KeyType key);
    SearchResult<KeyType, ORDER> mSearch(KeyType key);

    // OPERAÇÕES DE BUFFER POOL:
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

    BTreeNode<KeyType, ORDER>* allocateNode(){
        BTreeNode<KeyType, ORDER>* node = new BTreeNode<KeyType, ORDER>();
        node->diskIdx = DiskIO<KeyType, ORDER>::allocateNewIdx(filename);
        cache[node->diskIdx] = node;
        return node;
    }

    void markDirty(BTreeNode<KeyType, ORDER>* node) { node->dirty = true; }

    void flush(){ // Salva toda estrutura da ram na memória secundária, mas não deleta da memória principal
        for (auto& [idx, node] : cache) {
            if (node->dirty) saveNode(node);
        }
    }

private:
    std::string filename; // caminho do arquivo .dat onde a árvore é armazenada e persisitida
    std::unordered_map<int, BTreeNode<KeyType, ORDER>*> cache; // mapeia diskIdx → ponteiro do nó em RAM
    BTreeNode<KeyType, ORDER>* root;
    int rootIdx;   // diskIdx do root no arquivo
    
    // MÉTODOS PRIVADOS
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


    // MÉTODOS DE TRADUÇÃO ENTRE MEMÓRIA PRINCIPAL E SECUNDÁRIA /////////////
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
        node->dirty = false;          // veio fresquinho do disco, igual ao que estava lá
        // node->filho[i] já está como nullptr pelo construtor → lazy load on demand
        return node;
    }

};

#endif
