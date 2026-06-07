// Teste de correcao: opera a BTree e um std::set (oraculo) em paralelo,
// verificando que ambos concordam apos cada operacao. Roda com varias ORDERs.
#include "btree.h"
#include <set>
#include <random>
#include <vector>
#include <cstdio>
#include <string>
#include <cstdlib>

template <int ORDER>
bool runTest(int nOps, unsigned seed, int cache) {
    std::string path = "test_O" + std::to_string(ORDER) + ".dat";
    std::remove(path.c_str());
    std::remove((path + ".meta").c_str());

    std::set<int> oracle;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> keyDist(1, nOps / 2 + 1); // colisoes frequentes
    std::uniform_int_distribution<int> opDist(0, 2);

    {
        BTree<int, ORDER> tree(path, cache);
        for (int step = 0; step < nOps; step++) {
            int op = opDist(rng);
            int key = keyDist(rng);

            if (op == 2) { // remove
                bool got = tree.remove(key);
                bool exp = oracle.erase(key) > 0;
                if (got != exp) {
                    std::printf("[O=%d] FALHA remove(%d): got=%d exp=%d (step %d)\n",
                                ORDER, key, got, exp, step);
                    return false;
                }
            } else {       // insert
                if (oracle.insert(key).second) tree.insert(key);
                // (so inserimos quando ainda nao existe, para a busca exata bater)
            }

            // Verifica uma chave aleatoria a cada passo.
            int probe = keyDist(rng);
            bool got = tree.search(probe).found;
            bool exp = oracle.count(probe) > 0;
            if (got != exp) {
                std::printf("[O=%d] FALHA search(%d): got=%d exp=%d (step %d)\n",
                            ORDER, probe, got, exp, step);
                return false;
            }
        }

        // Verificacao final exaustiva sobre todo o dominio de chaves.
        for (int k = 0; k <= nOps / 2 + 2; k++) {
            bool got = tree.search(k).found;
            bool exp = oracle.count(k) > 0;
            if (got != exp) {
                std::printf("[O=%d] FALHA final search(%d): got=%d exp=%d\n",
                            ORDER, k, got, exp);
                return false;
            }
        }
    }

    // Reabre do disco e re-verifica (testa persistencia .dat/.meta).
    {
        BTree<int, ORDER> tree(path, cache);
        for (int k = 0; k <= nOps / 2 + 2; k++) {
            bool got = tree.search(k).found;
            bool exp = oracle.count(k) > 0;
            if (got != exp) {
                std::printf("[O=%d] FALHA pos-reabertura search(%d): got=%d exp=%d\n",
                            ORDER, k, got, exp);
                return false;
            }
        }
    }

    std::remove(path.c_str());
    std::remove((path + ".meta").c_str());
    std::printf("[O=%d] OK  (%d ops, cache=%d, oraculo final=%zu chaves)\n",
                ORDER, nOps, cache, oracle.size());
    return true;
}

int main() {
    bool ok = true;
    for (unsigned seed = 1; seed <= 5; seed++) {
        ok &= runTest<4>(20000, seed, 3);
        ok &= runTest<5>(20000, seed, 4);
        ok &= runTest<6>(20000, seed, 2);
        ok &= runTest<8>(20000, seed, 5);
    }
    std::printf(ok ? "\nTODOS OS TESTES PASSARAM\n" : "\nHOUVE FALHAS\n");
    return ok ? 0 : 1;
}