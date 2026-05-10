#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <string>
#include <fstream>
#include "btree.h"

// Arquivo gerado via Claude Code

template <typename KeyType, int ORDER>
class DiskIO {
public:
    // Cria (ou trunca) um arquivo binario vazio.
    static void createFile(const std::string& filename) {
        std::ofstream out(filename, std::ios::binary);
    }

    // Retorna o proximo indice livre. Indices comecam em 1
    // (o valor 0 e' reservado como sentinela "sem filho").
    static int allocateNewIdx(const std::string& filename) {
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file) return 1;
        std::streampos size = file.tellg();
        return static_cast<int>(size / sizeof(NodeRecord<KeyType, ORDER>)) + 1;
    }

    // Grava 'record' no slot 'index' (offset = (index-1) * sizeof(Record)).
    // Pre-condicao: o arquivo ja foi criado via createFile.
    static bool writeRecord(const std::string& filename, int index,
                            const NodeRecord<KeyType, ORDER>& record) {
        std::fstream file(filename, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) return false;
        file.seekp((index - 1) * sizeof(NodeRecord<KeyType, ORDER>));
        file.write(reinterpret_cast<const char*>(&record),
                   sizeof(NodeRecord<KeyType, ORDER>));
        return file.good();
    }

    // Le o slot 'index' para dentro de 'record'.
    static bool readRecord(const std::string& filename, int index,
                           NodeRecord<KeyType, ORDER>& record) {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;
        file.seekg((index - 1) * sizeof(NodeRecord<KeyType, ORDER>));
        file.read(reinterpret_cast<char*>(&record),
                  sizeof(NodeRecord<KeyType, ORDER>));
        return file.good();
    }
};

#endif
