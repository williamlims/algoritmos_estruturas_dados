#include <iostream>
#include <fstream>
#include <string>
using namespace std;

const int N = 3;

struct Node {
    int n;
    int A[N]; // Filhos
    int K[N - 1]; // Keys array: max_keys = N - 1
};

struct SearchResult {
    int nodeIndex;
    int keyIndex;
    bool found;
};

class DiskIO {
public:
    static void createFile(const string& filename) {
        ofstream out(filename, ios::binary);
    }

    static bool writeNode(const string& filename, int index, const Node& node) {
        fstream file(filename, ios::binary | ios::in | ios::out);
        if (!file) return false;
        
        file.seekp((index - 1) * sizeof(Node));
        file.write(reinterpret_cast<const char*>(&node), sizeof(Node));
        return file.good();
    }

    static bool readNode(const string& filename, int index, Node& node) {
        ifstream file(filename, ios::binary);
        if (!file) return false;
        
        file.seekg((index - 1) * sizeof(Node));
        file.read(reinterpret_cast<char*>(&node), sizeof(Node));
        return file.good();
    }
};

SearchResult mSearch(const string& filename, int rootIndex, int x) {
    int p = rootIndex;
    int q = 0;
    int i = 0;
    Node node;

    while (p != 0) {
        if (!DiskIO::readNode(filename, p, node)) {
            cerr << "Erro fatal de IO lendo disco no índice " << p << endl;
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

int main() {

    DiskIO diskManager;

    string dbFile = "arvoreB.dat";
    diskManager.createFile(dbFile);

    
    Node noA = {2, {2, 3, 4}, {20, 40}};
    diskManager.writeNode(dbFile, 1, noA);

    Node noB = {2, {0, 0, 0}, {10, 15}};
    diskManager.writeNode(dbFile, 2, noB);

    Node noC = {2, {0, 0, 5}, {25, 30}};
    diskManager.writeNode(dbFile, 3, noC);

    Node noD = {2, {0, 0, 0}, {45, 50}};
    diskManager.writeNode(dbFile, 4, noD);

    Node noE = {1, {0, 0, 0}, {35, 0}}; 
    diskManager.writeNode(dbFile, 5, noE);

    cout << "Arvore escrita no disco com sucesso!\n";
    cout << "------------------------------------\n";

    int root = 1;
    
    int testes[] = {25, 35, 50, 42, 18};
    
    for (int alvo : testes) {
        cout << "Buscando o valor " << alvo << "... ";
        SearchResult res = mSearch(dbFile, root, alvo);
        
        if (res.found) {
            cout << "ENCONTRADO! (Lido no bloco/nó " << res.nodeIndex << ", na posicao K[" << res.keyIndex << "])\n";
        } else {
            cout << "NÃO ENCONTRADO." << res.nodeIndex << ")\n";
        }
    }

    return 0;
}