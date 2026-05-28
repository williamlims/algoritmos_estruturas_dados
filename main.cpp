#include "btree_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
    // Uso: ./main [cacheSize] [datasetPath]
    //   cacheSize    padrao 3
    //   datasetPath  padrao datasets/n100000.csv
    int cacheSize = (argc >= 2) ? std::stoi(argv[1]) : 3;
    std::string datasetPath = (argc >= 3) ? argv[2] : "datasets/n100000.csv";

    BTreeManager<int, 4> mgr("arvores/demo.dat", cacheSize);
    std::cout << "Buffer pool: " << mgr.getMaxCacheSize() << " no(s)\n";

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
        mgr.Insert(id);
        total++;
    }
    std::cout << "Inseridos " << total << " registros\n";

    mgr.flush();   // forca gravacao de todos os nos dirty
    std::cout << "Escritas em disco (apos flush): " << mgr.getDiskWrites() << "\n";
    std::cout << "Leituras em disco: " << mgr.getDiskReads() << "\n";

    // Smoke test de busca
    for (int alvo : {1, 45, 32, 100002, 999999, 200, 0, 10000000, 1000001}) {
        auto res = mgr.mSearch(alvo);
        std::cout << "Busca " << alvo << ": "
                  << (res.found ? "ENCONTRADO" : "nao encontrado") << "\n";
    }

    // Impressao da estrutura hierarquica (limitada a 3 niveis pra nao explodir o stdout)
    std::cout << "\n--- Estrutura da arvore (ate nivel 3) ---\n";
    mgr.printTree(3);

    return 0;
}
