#include "btree.h"
#include "disk_manager.h"
#include <iostream>

SearchResult mSearch(const std::string& filename, int rootIndex, int x) {
    int p = rootIndex;
    int q = 0;
    int i = 0;
    Node node;

    while (p != 0) {
        if (!DiskIO::readNode(filename, p, node)) {
            std::cerr << "Erro fatal de IO lendo disco no indice " << p << std::endl;
            break;
        }

        i = 0;
        while (i < node.n && x >= node.K[i]) {
            if (x == node.K[i]) {
                return {p, i, true};
            }
            i++;
        }

        q = p;
        p = node.A[i];
    }

    return {q, i, false};
}