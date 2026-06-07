#ifndef DISK_MANAGER_TPP
#define DISK_MANAGER_TPP

// Implementacao de DiskIO. Incluido por disk_manager.h — nao compilar sozinho.

template <typename KeyType, int ORDER>
DiskIO<KeyType, ORDER>::DiskIO(const std::string& filename)
    : filename_(filename), nextIdx_(1), reads_(0), writes_(0) {

    // Garante que o arquivo exista antes de abrir em modo in|out.
    // (abrir em in|out num arquivo inexistente falha; ofstream o cria.)
    {
        std::ifstream probe(filename_, std::ios::binary);
        if (!probe) {
            std::ofstream create(filename_, std::ios::binary);
        }
    }

    file_.open(filename_, std::ios::binary | std::ios::in | std::ios::out);

    // Proximo indice livre = (tamanho do arquivo / tamanho do registro) + 1.
    file_.seekg(0, std::ios::end);
    std::streamoff size = file_.tellg();
    if (size > 0) {
        nextIdx_ = static_cast<int>(size / kRecordSize) + 1;
    }
}

template <typename KeyType, int ORDER>
DiskIO<KeyType, ORDER>::~DiskIO() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

template <typename KeyType, int ORDER>
int DiskIO<KeyType, ORDER>::allocateIdx() {
    return nextIdx_++;
}

template <typename KeyType, int ORDER>
void DiskIO<KeyType, ORDER>::readRecord(int index, NodeRecord<KeyType, ORDER>& rec) {
    // clear() limpa eofbit/failbit de operacoes anteriores; sem isso, um seek
    // apos um read que tocou o fim do arquivo falharia silenciosamente.
    file_.clear();
    file_.seekg((static_cast<std::streamoff>(index) - 1) * kRecordSize);
    file_.read(reinterpret_cast<char*>(&rec), sizeof(rec));
    ++reads_;
}

template <typename KeyType, int ORDER>
void DiskIO<KeyType, ORDER>::writeRecord(int index, const NodeRecord<KeyType, ORDER>& rec) {
    file_.clear();
    file_.seekp((static_cast<std::streamoff>(index) - 1) * kRecordSize);
    file_.write(reinterpret_cast<const char*>(&rec), sizeof(rec));
    file_.flush();   // garante durabilidade e consistencia leitura-apos-escrita
    ++writes_;
}

template <typename KeyType, int ORDER>
void DiskIO<KeyType, ORDER>::sync() {
    file_.flush();
}

#endif // DISK_MANAGER_TPP
