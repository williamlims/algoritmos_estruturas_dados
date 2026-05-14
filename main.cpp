#include "btree_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>

int main() {
    BTreeManager<int, 4> mgr("arvore.dat");

    std::ifstream csv("dataset.csv");
    if (!csv) { std::cerr << "Nao abriu dataset.csv\n"; return 1; }

    std::string linha;
    std::getline(csv, linha);  // descarta cabecalho

    int total = 0;
    while (std::getline(csv, linha)) {
        std::stringstream ss(linha);
        std::string campo;
        std::getline(ss, campo, ',');   // primeira coluna = id
        int id = std::stoi(campo);
        mgr.Insert(id);
        total++;
    }
    std::cout << "Inseridos " << total << " registros\n";

    // Smoke test de busca
    for (int alvo : {1, 45, 32, 100002, 200, 10000000, 1000001}) {
        auto res = mgr.mSearch(alvo);
        std::cout << "Busca " << alvo << ": "
                  << (res.found ? "ENCONTRADO" : "nao encontrado") << "\n";
    }
    return 0;
}
