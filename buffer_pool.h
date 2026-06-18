#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H

#include <unordered_map>
#include <list>
#include "node.h"
#include "disk_manager.h"

// ============================================================================
//  buffer_pool.h  —  BufferPool: cache de nos em RAM sobre o DiskIO.
//
//  Responsabilidades:
//    * manter ate maxCacheSize nos vivos em memoria (mapa diskIdx -> no);
//    * politica de substituicao LRU (Least Recently Used);
//    * pinagem: um no "pinado" nunca e' despejado (o algoritmo da arvore
//      pina os nos que esta usando para que ponteiros nao sejam invalidados);
//    * traduzir BTreeNode <-> NodeRecord e delegar o I/O fisico ao DiskIO;
//    * writeback: so grava em disco nos marcados como dirty.
//
//  A B-Tree fala APENAS com esta classe para obter nos. Ela nao sabe que
//  existe arquivo nem como o cache decide o que manter — separacao de camadas.
//
//  Sobre ponteiros: fetch()/allocate() retornam ponteiros para nos no cache.
//  Esses ponteiros sao validos enquanto o no nao for despejado. Como qualquer
//  fetch/allocate pode disparar um despejo (trimCache), o chamador deve pin()
//  os nos cujo ponteiro pretende usar atraves de chamadas subsequentes.
// ============================================================================
template <typename KeyType, int ORDER>
class BufferPool {
public:
    using Node = BTreeNode<KeyType, ORDER>;

    // 'io' deve sobreviver ao BufferPool (guardamos uma referencia).
    BufferPool(DiskIO<KeyType, ORDER>& io, int maxCacheSize);

    // Grava nos dirty e libera toda a memoria do cache.
    ~BufferPool();

    // Cria um no novo (em RAM), ja com diskIdx reservado, e o insere no cache.
    Node* allocate();

    // Reaproveita um slot de disco JA EXISTENTE (vindo de uma free-list): cria
    // um no zerado preso a 'diskIdx', descartando qualquer copia obsoleta desse
    // slot no cache (sem writeback). Usado pela B-Tree para reciclar nos.
    Node* allocateAt(int diskIdx);

    // Descarta um no do cache SEM gravar em disco (o conteudo foi liberado pela
    // arvore). Limpa tambem entradas de LRU e de pinagem desse slot.
    void evict(int diskIdx);

    // Devolve o no de indice diskIdx: cache hit, ou leitura de disco (miss).
    Node* fetch(int diskIdx);

    // Marca um no como alterado (sera gravado no proximo writeback).
    void markDirty(Node* node) { node->dirty = true; }

    // Pinagem (impede despejo). pin/unpin sao balanceados pelo chamador.
    void pin(int diskIdx);
    void unpin(int diskIdx);
    bool isPinned(int diskIdx) const;

    // Grava em disco todos os nos dirty (sem despeja-los).
    void flush();

    // Inspecao do pool.
    int  size()    const { return static_cast<int>(cache_.size()); }
    int  maxSize() const { return maxCacheSize_; }
    void setMaxSize(int n) { maxCacheSize_ = n; trimCache(); }

private:
    DiskIO<KeyType, ORDER>& io_;
    int maxCacheSize_;

    std::unordered_map<int, Node*> cache_;          // diskIdx -> no em RAM

    // LRU: front = mais recente (MRU), back = menos recente.
    std::list<int>                                   lruOrder_;
    std::unordered_map<int, std::list<int>::iterator> lruPos_;

    // Pinagem: diskIdx -> contagem de pins (>0 => protegido contra despejo).
    std::unordered_map<int, int> pinCount_;

    // --- helpers internos ---
    void touchLRU(int diskIdx);   // marca diskIdx como o mais recente
    void trimCache();             // despeja LRU nao-pinados ate caber
    void saveNode(Node* node);    // traduz e grava via DiskIO (se chamado)
    Node* loadFromDisk(int diskIdx);

    NodeRecord<KeyType, ORDER> nodeToRecord(const Node* node) const;
    Node* recordToNode(const NodeRecord<KeyType, ORDER>& rec, int diskIdx) const;
};

#include "buffer_pool.tpp"

#endif // BUFFER_POOL_H