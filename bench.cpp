// Driver de benchmark da B-Tree.
// Compilado por ordem via -DBENCH_ORDER=N.
// Mede tempo (chrono) + acessos a disco (contadores do DiskIO, reexpostos pela
// BTree) para as fases insert e search, emitindo UMA linha "RESULT,..." em stdout.

#include "btree.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cstdlib>

// Macro com prefixo proprio: usar "ORDER" colidiria com o parametro de
// template "int ORDER" dos headers (o preprocessador o substituiria).
#ifndef BENCH_ORDER
#define BENCH_ORDER 4
#endif
constexpr int kOrder = BENCH_ORDER;

namespace {

struct Args {
    std::string phase;     // "insert" | "search"
    std::string tree;      // caminho do .dat
    std::string dataset;   // caminho do .csv (insert)
    int cache   = 3;
    int n       = 10000;   // qtd de buscas (search)
    int seed    = 42;
    int nKeys   = 0;       // = tamanho do dataset (search gera queries sem reler CSV)
};

// Parse manual no estilo do main.cpp. Em erro, escreve no stderr e aborta.
Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string flag = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Faltou valor para " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if      (flag == "--phase")   a.phase   = next("--phase");
        else if (flag == "--tree")    a.tree    = next("--tree");
        else if (flag == "--dataset") a.dataset = next("--dataset");
        else if (flag == "--cache")   a.cache   = std::stoi(next("--cache"));
        else if (flag == "--n")       a.n       = std::stoi(next("--n"));
        else if (flag == "--seed")    a.seed    = std::stoi(next("--seed"));
        else if (flag == "--n-keys")  a.nKeys   = std::stoi(next("--n-keys"));
        else {
            std::cerr << "Flag desconhecida: " << flag << "\n";
            std::exit(2);
        }
    }
    if (a.phase != "insert" && a.phase != "search") {
        std::cerr << "--phase deve ser insert|search\n";
        std::exit(2);
    }
    if (a.tree.empty()) {
        std::cerr << "--tree e' obrigatorio\n";
        std::exit(2);
    }
    return a;
}

// Le a coluna 0 (id) de cada linha do CSV, descartando o cabecalho.
std::vector<int> lerIds(const std::string& path) {
    std::ifstream csv(path);
    if (!csv) {
        std::cerr << "Nao abriu dataset: " << path << "\n";
        std::exit(1);
    }
    std::vector<int> ids;
    std::string linha;
    std::getline(csv, linha); // descarta cabecalho
    while (std::getline(csv, linha)) {
        if (linha.empty()) continue;
        std::stringstream ss(linha);
        std::string campo;
        std::getline(ss, campo, ',');
        ids.push_back(std::stoi(campo));
    }
    return ids;
}

void emitResult(const Args& a, double timeS, int reads, int writes, int found) {
    std::cout << "RESULT"
              << ",order="  << kOrder
              << ",cache="  << a.cache
              << ",phase="  << a.phase
              << ",n="      << a.n
              << ",time_s=" << timeS
              << ",reads="  << reads
              << ",writes=" << writes
              << ",found="  << found
              << "\n";
}

void runInsert(Args& a) {
    if (a.dataset.empty()) {
        std::cerr << "--dataset e' obrigatorio no insert\n";
        std::exit(2);
    }
    std::vector<int> ids = lerIds(a.dataset); // fora do relogio
    a.n = static_cast<int>(ids.size());

    BTree<int, kOrder> tree(a.tree, a.cache);
    tree.resetDiskCounters();

    auto t0 = std::chrono::steady_clock::now();
    for (int id : ids) tree.insert(id);
    tree.flush();
    auto t1 = std::chrono::steady_clock::now();

    double timeS = std::chrono::duration<double>(t1 - t0).count();
    emitResult(a, timeS, tree.diskReads(), tree.diskWrites(), 0);
    // destrutor grava .meta e libera cache
}

void runSearch(Args& a) {
    if (a.nKeys <= 0) {
        std::cerr << "--n-keys (tamanho do dataset) e' obrigatorio no search\n";
        std::exit(2);
    }
    // Gera as queries ANTES do relogio: 50% existentes (1..N), 50% ausentes (>N).
    std::mt19937 rng(static_cast<unsigned>(a.seed));
    std::uniform_int_distribution<int> existente(1, a.nKeys);
    std::uniform_int_distribution<int> ausente(a.nKeys + 1, a.nKeys + 1000000);
    std::vector<int> queries;
    queries.reserve(a.n);
    for (int i = 0; i < a.n; i++)
        queries.push_back((i % 2 == 0) ? existente(rng) : ausente(rng));

    BTree<int, kOrder> tree(a.tree, a.cache); // arvore ja deve existir
    tree.resetDiskCounters();

    volatile int found = 0; // volatile evita que o laco seja otimizado fora
    auto t0 = std::chrono::steady_clock::now();
    for (int q : queries) {
        if (tree.search(q).found) found = found + 1;
    }
    auto t1 = std::chrono::steady_clock::now();

    double timeS = std::chrono::duration<double>(t1 - t0).count();
    emitResult(a, timeS, tree.diskReads(), tree.diskWrites(), found);
}

} // namespace

int main(int argc, char** argv) {
    Args a = parseArgs(argc, argv);
    if (a.phase == "insert") runInsert(a);
    else                     runSearch(a);
    return 0;
}