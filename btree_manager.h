#ifndef BTREE_MANAGER_H
#define BTREE_MANAGER_H

#include <string>
#include <unordered_map>
#include <list>
#include <fstream>
#include <iostream>
#include "disk_manager.h"
#include "btree.h"

template <typename KeyType, int ORDER>
class BTreeManager {
public:

    BTreeManager(const std::string& filename, int maxCacheSize = 3): // Inicializa árvore .dat ou inicia .dat
        filename(filename), rootIdx(0), nextIdx(1),
        diskReads(0), diskWrites(0), maxCacheSize(maxCacheSize) {

        // Tenta carregar metadata de arvore existente
        std::ifstream meta(filename + ".meta");
        if (meta) {
            meta >> rootIdx;
            // Continua alocando a partir de onde paramos (baseado no tamanho do .dat)
            nextIdx = DiskIO<KeyType, ORDER>::allocateNewIdx(filename);
        } else {
            // Arvore nova: cria arquivo de dados vazio; nextIdx ja eh 1
            DiskIO<KeyType, ORDER>::createFile(filename);
        }
    }

    ~BTreeManager(){ // Executa flush e deleta árvore da memória principal
        flush();
        std::ofstream meta(filename + ".meta");
        meta << rootIdx;
        for (auto& [idx, node] : cache) delete node;
    }

    // OPERAÇÕES B-TREE: ///////////////////////////////////////////////////////////////
    void Insert(KeyType key) {
        // CASO 1: arvore vazia
        if (rootIdx == 0) {
            BTreeNode<KeyType, ORDER>* newRoot = allocateNode();
            rootIdx = newRoot->diskIdx;
            newRoot->keys[0] = key;
            newRoot->n = 1;
            markDirty(newRoot);
            return;
        }

        // CASO 2: raiz cheia → split de raiz
        pin(rootIdx);
        BTreeNode<KeyType, ORDER>* root = loadNode(rootIdx);
        if (root->n == ORDER - 1) {
            int oldRootIdx = rootIdx;

            BTreeNode<KeyType, ORDER>* newRoot = allocateNode();
            int newRootIdx = newRoot->diskIdx;
            pin(newRootIdx);

            newRoot->filhoIdx[0] = oldRootIdx;
            markDirty(newRoot);

            rootIdx = newRootIdx;
            splitNode(oldRootIdx, newRootIdx, 0);
            unpin(oldRootIdx);   // libera o antigo root

            // desce a partir do newRoot (que ja esta pinado)
            unpin(newRootIdx);
            insertNonFull(newRootIdx, key);
            return;
        }
        unpin(rootIdx);

        // CASO 3: raiz tem espaço
        insertNonFull(rootIdx, key);
    }

    SearchResult<KeyType, ORDER> mSearch(KeyType key) {
        if (rootIdx == 0) return {0, 0, false};

        int currentIdx = rootIdx;
        int lastIdx = 0;
        int lastI = 0;
        while (currentIdx != 0) {
            pin(currentIdx);
            BTreeNode<KeyType, ORDER>* node = loadNode(currentIdx);

            int i = 0;
            while (i < node->n && key >= node->keys[i]) {
                if (key == node->keys[i]) {
                    unpin(currentIdx);
                    return {currentIdx, i, true};
                }
                i++;
            }
            lastIdx = currentIdx;
            lastI = i;
            int nextChildIdx = node->filhoIdx[i];
            unpin(currentIdx);
            currentIdx = nextChildIdx;
        }
        return {lastIdx, lastI, false};
    }

    void deleteRegister(KeyType /*key*/){
    }

    void showRegister(KeyType /*key*/){
    }

    void printTree(int maxLevel = -1) {
        if (rootIdx == 0) {
            std::cout << "(arvore vazia)\n";
            return;
        }
        printNodeIdx(rootIdx, 0, maxLevel);
    }

    // CONTADOR DE ACESSOS A DISCO //////////////////////////////////////////////////////
    int getDiskReads() const { return diskReads; }
    int getDiskWrites() const { return diskWrites; }
    void resetDiskCounters() { diskReads = 0; diskWrites = 0; }

    // INSPECAO DO BUFFER POOL //////////////////////////////////////////////////////////
    int getCacheSize() const { return static_cast<int>(cache.size()); }
    int getMaxCacheSize() const { return maxCacheSize; }
    void setMaxCacheSize(int n) { maxCacheSize = n; trimCache(); }

    void flush(){ // Salva toda estrutura da ram na memória secundária, mas não deleta da memória principal
        for (auto& [idx, node] : cache) {
            if (node->dirty) saveNode(node);
        }
    }

private:
    std::string filename;
    std::unordered_map<int, BTreeNode<KeyType, ORDER>*> cache; // diskIdx → ponteiro do nó em RAM
    int rootIdx;        // diskIdx do root no arquivo
    int nextIdx;        // proximo diskIdx livre
    int diskReads;      // contagem de leituras reais no disco (cache miss)
    int diskWrites;     // contagem de gravacoes reais no disco
    int maxCacheSize;   // capacidade do buffer pool em nos

    // LRU: front = mais recente, back = menos recente
    std::list<int> lruOrder;
    std::unordered_map<int, std::list<int>::iterator> lruPos;
    // Pinagem: nos com pinCount > 0 nao podem ser despejados
    std::unordered_map<int, int> pinCount;

    // PINAGEM /////////////////////////////////////////////////////////////////////////
    void pin(int diskIdx) {
        pinCount[diskIdx]++;
    }
    void unpin(int diskIdx) {
        auto it = pinCount.find(diskIdx);
        if (it == pinCount.end()) return;
        if (--it->second <= 0) pinCount.erase(it);
    }
    bool isPinned(int diskIdx) const {
        auto it = pinCount.find(diskIdx);
        return it != pinCount.end() && it->second > 0;
    }

    // LRU /////////////////////////////////////////////////////////////////////////////
    void touchLRU(int diskIdx) {
        auto it = lruPos.find(diskIdx);
        if (it != lruPos.end()) lruOrder.erase(it->second);
        lruOrder.push_front(diskIdx);
        lruPos[diskIdx] = lruOrder.begin();
    }

    // Despeja LRU nao-pinados ate caber em maxCacheSize.
    // Se todos pinados, retorna sem despejar (soft overflow temporario).
    void trimCache() {
        auto it = lruOrder.end();
        while ((int)cache.size() > maxCacheSize && it != lruOrder.begin()) {
            --it;
            int idx = *it;
            if (isPinned(idx)) continue;

            auto cit = cache.find(idx);
            if (cit != cache.end()) {
                BTreeNode<KeyType, ORDER>* node = cit->second;
                if (node->dirty) saveNode(node);
                delete node;
                cache.erase(cit);
            }
            it = lruOrder.erase(it);
            lruPos.erase(idx);
        }
    }

    // FUNÇÕES AUXILIARES //////////////////////////////////////////////////////////////

    // Insere key numa subarvore enraizada em nodeIdx que NAO esta cheia.
    // Mantem invariante: ao chamar split de filho, todos os nos envolvidos sao pinados.
    void insertNonFull(int nodeIdx, KeyType key) {
        pin(nodeIdx);
        BTreeNode<KeyType, ORDER>* node = loadNode(nodeIdx);
        int i = node->n - 1;

        // Folha: insere e termina
        if (node->filhoIdx[0] == 0) {
            while (i >= 0 && key < node->keys[i]) {
                node->keys[i + 1] = node->keys[i];
                i--;
            }
            node->keys[i + 1] = key;
            node->n++;
            markDirty(node);
            unpin(nodeIdx);
            return;
        }

        // Nao-folha: localiza filho
        while (i >= 0 && key < node->keys[i]) i--;
        i++;
        int childIdx = node->filhoIdx[i];

        // Split preemptivo se o filho estiver cheio
        pin(childIdx);
        BTreeNode<KeyType, ORDER>* child = loadNode(childIdx);
        if (child->n == ORDER - 1) {
            splitNode(childIdx, nodeIdx, i);
            node = loadNode(nodeIdx); // pinado, cache hit
            if (key > node->keys[i]) {
                unpin(childIdx);
                i++;
                childIdx = node->filhoIdx[i];
            } else {
                unpin(childIdx);
            }
        } else {
            unpin(childIdx);
        }

        // Descida em posicao de tail call: libera o pai antes de recursar
        unpin(nodeIdx);
        insertNonFull(childIdx, key);
    }

    // Divide nodeIdx (filho i de paiIdx) em dois nos. Promove chave do meio pro pai.
    // Caller deve garantir que nodeIdx e paiIdx estao pinados.
    void splitNode(int nodeIdx, int paiIdx, int i) {
        pin(nodeIdx);
        pin(paiIdx);

        BTreeNode<KeyType, ORDER>* newNode = allocateNode();
        int newNodeIdx = newNode->diskIdx;
        pin(newNodeIdx);

        BTreeNode<KeyType, ORDER>* node = loadNode(nodeIdx);
        BTreeNode<KeyType, ORDER>* pai  = loadNode(paiIdx);

        const int mid  = (ORDER - 1) / 2;
        const int newN = ORDER - 2 - mid;
        node->n    = mid;
        newNode->n = newN;

        // FASE 1: chaves da metade direita para newNode
        for (int j = 0; j < newN; j++)
            newNode->keys[j] = node->keys[j + mid + 1];

        // FASE 2: filhos da metade direita (so copia os indices, sem carregar)
        if (node->filhoIdx[0] != 0) {
            for (int j = 0; j <= newN; j++) {
                newNode->filhoIdx[j] = node->filhoIdx[j + mid + 1];
                node->filhoIdx[j + mid + 1] = 0;
            }
        }

        // FASE 3: insere newNode em pai (posicao i+1)
        for (int j = pai->n; j >= i + 1; j--) {
            pai->filhoIdx[j + 1] = pai->filhoIdx[j];
        }
        pai->filhoIdx[i + 1] = newNodeIdx;

        // FASE 4: promove chave do meio
        for (int j = pai->n - 1; j >= i; j--)
            pai->keys[j + 1] = pai->keys[j];
        pai->keys[i] = node->keys[mid];
        pai->n++;

        markDirty(node);
        markDirty(newNode);
        markDirty(pai);

        unpin(nodeIdx);
        unpin(paiIdx);
        unpin(newNodeIdx);
    }

    // IMPRESSAO RECURSIVA /////////////////////////////////////////////////////////////
    void printNodeIdx(int diskIdx, int level, int maxLevel) {
        if (diskIdx == 0) return;
        if (maxLevel >= 0 && level > maxLevel) return;

        pin(diskIdx);
        BTreeNode<KeyType, ORDER>* node = loadNode(diskIdx);

        for (int i = 0; i < level; i++) std::cout << "  ";
        std::cout << "[N" << diskIdx << "] ";
        for (int i = 0; i < node->n; i++) {
            std::cout << node->keys[i];
            if (i < node->n - 1) std::cout << " ";
        }
        std::cout << "\n";

        // Snapshot dos filhos antes da recursao, para liberar o pinado do pai
        bool isInternal = (node->filhoIdx[0] != 0);
        int childCount = isInternal ? node->n + 1 : 0;
        int childIdxs[ORDER];
        for (int i = 0; i < childCount; i++) childIdxs[i] = node->filhoIdx[i];

        unpin(diskIdx);
        for (int i = 0; i < childCount; i++) {
            printNodeIdx(childIdxs[i], level + 1, maxLevel);
        }
    }

    // BUFFER POOL /////////////////////////////////////////////////////////////////////
    BTreeNode<KeyType, ORDER>* allocateNode(){
        BTreeNode<KeyType, ORDER>* node = new BTreeNode<KeyType, ORDER>();
        node->diskIdx = nextIdx++;
        cache[node->diskIdx] = node;
        touchLRU(node->diskIdx);
        trimCache();
        return node;
    }

    void markDirty(BTreeNode<KeyType, ORDER>* node) { node->dirty = true; }

    void saveNode(BTreeNode<KeyType, ORDER>* node){
        NodeRecord<KeyType, ORDER> record = nodeToRecord(node);
        DiskIO<KeyType, ORDER>::writeRecord(filename, node->diskIdx, record);
        diskWrites++;
        node->dirty = false;
    }

    BTreeNode<KeyType, ORDER>* loadNode(int diskIdx){
        auto it = cache.find(diskIdx);
        if (it != cache.end()) {
            touchLRU(diskIdx);
            return it->second;
        }

        NodeRecord<KeyType, ORDER> record;
        DiskIO<KeyType, ORDER>::readRecord(filename, diskIdx, record);
        diskReads++;
        BTreeNode<KeyType, ORDER>* node = recordToNode(record, diskIdx);
        cache[diskIdx] = node;
        touchLRU(diskIdx);
        trimCache();
        return node;
    }

    // TRADUÇÃO ENTRE MEMÓRIA PRINCIPAL E SECUNDÁRIA ///////////////////////////////////
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
        node->dirty = false;
        return node;
    }

};

#endif
