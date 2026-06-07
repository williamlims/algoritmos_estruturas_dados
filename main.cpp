#include "btree.h"
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    // Uso: ./main [cacheSize] [datasetPath]
    //   cacheSize    padrao 3
    //   datasetPath  padrao datasets/n100000.csv
    int cacheSize = (argc >= 2) ? std::stoi(argv[1]) : 3;
    std::string datasetPath = (argc >= 3) ? argv[2] : "datasets/n100000.csv";

    BTree<int, 4> tree("arvores/demo.dat", cacheSize);
    std::cout << "Buffer pool: " << tree.maxCacheSize() << " no(s)\n";

    std::ifstream csv(datasetPath);
    if (!csv) {
        std::cerr << "Nao abriu " << datasetPath
                  << "\nGere com: python3 generate_dataset.py -n 100000 -o " << datasetPath << "\n";
        return 1;
    }

    std::string linha;
    std::getline(csv, linha);  // descarta cabecalho

    int total = 0;
    while (std::getline(csv, linha)) {
        std::stringstream ss(linha);
        std::string campo;
        std::getline(ss, campo, ',');   // primeira coluna = id
        int id = std::stoi(campo);
        tree.insert(id);
        total++;
    }
    std::cout << "Inseridos " << total << " registros\n";

    tree.flush();   // forca gravacao de todos os nos dirty
    std::cout << "Escritas em disco (apos flush): " << tree.diskWrites() << "\n";
    std::cout << "Leituras em disco: " << tree.diskReads() << "\n";

    // Smoke test de busca
    for (int alvo : {1, 45, 32, 100002, 999999, 200, 0, 10000000, 1000001}) {
        auto res = tree.search(alvo);
        std::cout << "Busca " << alvo << ": "
                  << (res.found ? "ENCONTRADO" : "nao encontrado") << "\n";
    }

    // Smoke test de remocao
    for (int alvo : {45, 200, 999999}) {
        bool ok = tree.remove(alvo);
        std::cout << "Remocao " << alvo << ": "
                  << (ok ? "removido" : "nao existia") << "\n";
    }

    // Impressao da estrutura hierarquica (limitada a 3 niveis).
    std::cout << "\n--- Estrutura da arvore (ate nivel 3) ---\n";
    tree.print(3);

    return 0;
}