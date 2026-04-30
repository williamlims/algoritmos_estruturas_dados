#include <iostream>
#include <string>
#include "btree.h"
#include "disk_manager.h"
using namespace std;

int main() {
    string dbFile = "arvoreB.dat";
    DiskIO::createFile(dbFile);

    Node noA = {2, {2, 3, 4}, {20, 40}};
    DiskIO::writeNode(dbFile, 1, noA);

    Node noB = {2, {0, 0, 0}, {10, 15}};
    DiskIO::writeNode(dbFile, 2, noB);

    Node noC = {2, {0, 0, 5}, {25, 30}};
    DiskIO::writeNode(dbFile, 3, noC);

    Node noD = {2, {0, 0, 0}, {45, 50}};
    DiskIO::writeNode(dbFile, 4, noD);

    Node noE = {1, {0, 0, 0}, {35, 0}};
    DiskIO::writeNode(dbFile, 5, noE);

    cout << "Arvore escrita no disco com sucesso!\n";
    cout << "------------------------------------\n";

    int root = 1;
    int testes[] = {25, 35, 50, 42, 18};

    for (int alvo : testes) {
        cout << "Buscando o valor " << alvo << "... ";
        SearchResult res = mSearch(dbFile, root, alvo);

        if (res.found) {
            cout << "ENCONTRADO! (Lido no no " << res.nodeIndex
                 << ", na posicao K[" << res.keyIndex << "])\n";
        } else {
            cout << "NAO ENCONTRADO. (parou no no " << res.nodeIndex << ")\n";
        }
    }

    return 0;
}