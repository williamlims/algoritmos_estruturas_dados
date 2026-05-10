// Teste de persistencia: carrega arvore.dat + .meta e faz mSearch
// sem reinserir. Se rodar com sucesso, prova que o disco esta consistente.
#include "btree_manager.h"
#include <iostream>

int main() {
    BTreeManager<int, 5> mgr("arvore.dat");

    int total_acertos = 0;
    int total_erros = 0;

    // Busca algumas chaves que devem existir (1..10000)
    for (int alvo : {1, 7, 42, 100, 500, 1234, 5000, 8765, 9999, 10000}) {
        auto res = mgr.mSearch(alvo);
        bool deveria = (alvo >= 1 && alvo <= 10000);
        bool ok = (res.found == deveria);
        if (ok) total_acertos++;
        else    total_erros++;
        std::cout << "  search(" << alvo << ") found=" << res.found
                  << "  esperado=" << deveria
                  << (ok ? "  OK" : "  FAIL") << "\n";
    }

    // Algumas que NAO devem existir
    for (int alvo : {0, -1, 10001, 99999, 1000000}) {
        auto res = mgr.mSearch(alvo);
        bool ok = (res.found == false);
        if (ok) total_acertos++;
        else    total_erros++;
        std::cout << "  search(" << alvo << ") found=" << res.found
                  << "  esperado=0"
                  << (ok ? "  OK" : "  FAIL") << "\n";
    }

    std::cout << "\nResultado: " << total_acertos << " OK / "
              << total_erros << " FAIL\n";
    return total_erros == 0 ? 0 : 1;
}
