#ifndef BUFFER_POOL_TPP
#define BUFFER_POOL_TPP

// Implementacao de BufferPool. Incluido por buffer_pool.h.

template <typename KeyType, int ORDER>
BufferPool<KeyType, ORDER>::BufferPool(DiskIO<KeyType, ORDER>& io, int maxCacheSize)
    : io_(io), maxCacheSize_(maxCacheSize) {}

template <typename KeyType, int ORDER>
BufferPool<KeyType, ORDER>::~BufferPool() {
    flush();
    for (auto& [idx, node] : cache_) delete node;
}

// ---------------------------------------------------------------------------
// PINAGEM
// ---------------------------------------------------------------------------
template <typename KeyType, int ORDER>
void BufferPool<KeyType, ORDER>::pin(int diskIdx) {
    pinCount_[diskIdx]++;
}

template <typename KeyType, int ORDER>
void BufferPool<KeyType, ORDER>::unpin(int diskIdx) {
    auto it = pinCount_.find(diskIdx);
    if (it == pinCount_.end()) return;
    if (--it->second <= 0) pinCount_.erase(it);
}

template <typename KeyType, int ORDER>
bool BufferPool<KeyType, ORDER>::isPinned(int diskIdx) const {
    auto it = pinCount_.find(diskIdx);
    return it != pinCount_.end() && it->second > 0;
}

// ---------------------------------------------------------------------------
// LRU
// ---------------------------------------------------------------------------
template <typename KeyType, int ORDER>
void BufferPool<KeyType, ORDER>::touchLRU(int diskIdx) {
    auto it = lruPos_.find(diskIdx);
    if (it != lruPos_.end()) lruOrder_.erase(it->second);
    lruOrder_.push_front(diskIdx);
    lruPos_[diskIdx] = lruOrder_.begin();
}

// Despeja nos nao-pinados, do menos recente para o mais recente, ate caber em
// maxCacheSize_. Nunca despeja o MRU (front): e' sempre o no recem
// carregado/alocado, que o chamador esta prestes a usar mas ainda nao pinou.
// Se todos os candidatos estao pinados ou sao o MRU, retorna sem despejar
// (overflow temporario tolerado).
template <typename KeyType, int ORDER>
void BufferPool<KeyType, ORDER>::trimCache() {
    int mru = lruOrder_.empty() ? 0 : lruOrder_.front();
    auto it = lruOrder_.end();
    while (static_cast<int>(cache_.size()) > maxCacheSize_ && it != lruOrder_.begin()) {
        --it;
        int idx = *it;
        if (idx == mru || isPinned(idx)) continue;

        auto cit = cache_.find(idx);
        if (cit != cache_.end()) {
            Node* node = cit->second;
            if (node->dirty) saveNode(node);
            delete node;
            cache_.erase(cit);
        }
        it = lruOrder_.erase(it);
        lruPos_.erase(idx);
    }
}

// ---------------------------------------------------------------------------
// ALOCACAO / BUSCA DE NOS
// ---------------------------------------------------------------------------
template <typename KeyType, int ORDER>
typename BufferPool<KeyType, ORDER>::Node*
BufferPool<KeyType, ORDER>::allocate() {
    Node* node = new Node();
    node->diskIdx = io_.allocateIdx();
    cache_[node->diskIdx] = node;
    touchLRU(node->diskIdx);
    trimCache();
    return node;
}

template <typename KeyType, int ORDER>
typename BufferPool<KeyType, ORDER>::Node*
BufferPool<KeyType, ORDER>::fetch(int diskIdx) {
    auto it = cache_.find(diskIdx);
    if (it != cache_.end()) {        // cache hit: nenhum acesso a disco
        touchLRU(diskIdx);
        return it->second;
    }
    Node* node = loadFromDisk(diskIdx);  // cache miss: 1 leitura de disco
    cache_[diskIdx] = node;
    touchLRU(diskIdx);
    trimCache();
    return node;
}

template <typename KeyType, int ORDER>
void BufferPool<KeyType, ORDER>::flush() {
    for (auto& [idx, node] : cache_) {
        if (node->dirty) saveNode(node);
    }
    io_.sync();
}

// ---------------------------------------------------------------------------
// PERSISTENCIA (delega o I/O fisico ao DiskIO)
// ---------------------------------------------------------------------------
template <typename KeyType, int ORDER>
void BufferPool<KeyType, ORDER>::saveNode(Node* node) {
    NodeRecord<KeyType, ORDER> rec = nodeToRecord(node);
    io_.writeRecord(node->diskIdx, rec);
    node->dirty = false;
}

template <typename KeyType, int ORDER>
typename BufferPool<KeyType, ORDER>::Node*
BufferPool<KeyType, ORDER>::loadFromDisk(int diskIdx) {
    NodeRecord<KeyType, ORDER> rec;
    io_.readRecord(diskIdx, rec);
    return recordToNode(rec, diskIdx);
}

// ---------------------------------------------------------------------------
// TRADUCAO MEMORIA <-> DISCO
// ---------------------------------------------------------------------------
template <typename KeyType, int ORDER>
NodeRecord<KeyType, ORDER>
BufferPool<KeyType, ORDER>::nodeToRecord(const Node* node) const {
    NodeRecord<KeyType, ORDER> rec;
    rec.n = node->n;
    for (int i = 0; i < ORDER - 1; i++) rec.keys[i]     = node->keys[i];
    for (int i = 0; i < ORDER;     i++) rec.filhoIdx[i] = node->filhoIdx[i];
    return rec;
}

template <typename KeyType, int ORDER>
typename BufferPool<KeyType, ORDER>::Node*
BufferPool<KeyType, ORDER>::recordToNode(const NodeRecord<KeyType, ORDER>& rec,
                                         int diskIdx) const {
    Node* node = new Node();
    node->n = rec.n;
    for (int i = 0; i < ORDER - 1; i++) node->keys[i]     = rec.keys[i];
    for (int i = 0; i < ORDER;     i++) node->filhoIdx[i] = rec.filhoIdx[i];
    node->diskIdx = diskIdx;
    node->dirty = false;   // recem-lido do disco: limpo
    return node;
}

#endif // BUFFER_POOL_TPP
