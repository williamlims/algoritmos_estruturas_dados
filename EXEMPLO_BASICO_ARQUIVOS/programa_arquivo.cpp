#include <iostream>
#include <fstream>
#include <cstring>

using namespace std;

#define TAM 6

class Produto {
public:
    int codigo;
    char nome[100];

    Produto() {
        codigo = 0;
        strcpy(nome, "");
    }

    Produto(int c, const char* n) {
        codigo = c;
        strcpy(nome, n);
    }

    void imprimir() {
        printf("%06d %s\n", codigo, nome);
    }
};

class Cadastro {
private:
    string nomeArquivo;

public:
    Cadastro(string nome) {
        nomeArquivo = nome;
    }

    void salvarInicial(Produto produtos[], int tamanho) {
        ofstream arq(nomeArquivo.c_str(), ios::binary);

        if (!arq) {
            cout << "Erro ao abrir arquivo.\n";
            return;
        }

        arq.write(reinterpret_cast<char*>(produtos), sizeof(Produto) * tamanho);
        arq.close();
    }

    void imprimirCadastro() {
        ifstream arq(nomeArquivo.c_str(), ios::binary);
        Produto p;

        if (!arq) {
            cout << "Erro ao abrir arquivo.\n";
            return;
        }

        cout << "Imprime Produtos\n";

        while (arq.read(reinterpret_cast<char*>(&p), sizeof(Produto))) {
            p.imprimir();
        }

        cout << endl;
        arq.close();
    }

    void alterarNome(int codigo, const char* novoNome) {
        fstream arq(nomeArquivo.c_str(), ios::in | ios::out | ios::binary);
        Produto p;

        if (!arq) {
            cout << "Erro ao abrir arquivo.\n";
            return;
        }

        while (arq.read(reinterpret_cast<char*>(&p), sizeof(Produto))) {
            if (p.codigo == codigo) {

                strcpy(p.nome, novoNome);

                arq.seekp(-1 * sizeof(Produto), ios::cur);
                arq.write(reinterpret_cast<char*>(&p), sizeof(Produto));

                break;
            }
        }

        arq.close();
    }
};

int main() {

    Cadastro cadastro("produtos.dat");

    Produto lista[TAM] = {
        Produto(11, "Vaso"),
        Produto(12, "Caneta"),
        Produto(13, "Livro"),
        Produto(14, "Espelho"),
        Produto(15, "Bolsa"),
        Produto(16, "Prato")
    };

    cadastro.salvarInicial(lista, TAM);

    cadastro.imprimirCadastro();

    cadastro.alterarNome(12, "Caneca");

    cadastro.imprimirCadastro();

    return 0;
}