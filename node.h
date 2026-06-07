#ifndef NODE_H
#define NODE_H

// ============================================================================
//  node.h  —  Estruturas de dados puras da B-Tree.
//
//  Este header NAO contem logica de algoritmo, de disco ou de cache.
//  Define apenas como um no e' representado em memoria principal
//  (BTreeNode), como ele e' serializado em disco (NodeRecord) e o
//  resultado de uma busca (SearchResult).
//
//  Convencao de indices de disco: o indice 0 e' reservado como sentinela
//  "sem filho" / "no inexistente". Indices validos comecam em 1.
// ============================================================================

// ----------------------------------------------------------------------------
// No da arvore como vive na memoria principal (dentro do buffer pool).
//
// Capacidade: ate ORDER-1 chaves e ate ORDER filhos.
// Um no e' FOLHA sse filhoIdx[0] == 0 (nao possui filhos).
// ----------------------------------------------------------------------------
template <typename KeyType, int ORDER>
class BTreeNode {
public:
    KeyType keys[ORDER - 1];   // chaves ordenadas em ordem crescente: keys[0..n-1]
    int     n;                 // quantidade de chaves atualmente armazenadas

    int  filhoIdx[ORDER];      // indices em disco dos filhos (0 = sem filho)
    int  diskIdx;              // indice deste no no arquivo (sua "identidade")
    bool dirty;                // true se o no foi alterado em RAM e ainda nao
                               // foi gravado em disco (precisa de writeback)

    BTreeNode() : n(0), diskIdx(0), dirty(true) {
        for (int i = 0; i < ORDER; i++) filhoIdx[i] = 0;
    }

    // Conveniencia: um no e' folha quando nao tem nenhum filho.
    bool isLeaf() const { return filhoIdx[0] == 0; }
};

// ----------------------------------------------------------------------------
// Imagem de um no como gravada no disco. E' um POD de tamanho fixo
// (sizeof estavel), permitindo enderecamento por slot:
//     offset(index) = (index - 1) * sizeof(NodeRecord)
// Repare que NAO guardamos diskIdx nem dirty: o indice e' o proprio slot e
// "dirty" so faz sentido em memoria.
// ----------------------------------------------------------------------------
template <typename KeyType, int ORDER>
struct NodeRecord {
    int     n;
    KeyType keys[ORDER - 1];
    int     filhoIdx[ORDER];   // 0 = sem filho
};

// ----------------------------------------------------------------------------
// Resultado de uma busca.
//
//   found    : true se a chave foi encontrada.
//   nodeIdx  : se found, o no que contem a chave;
//              se !found, a FOLHA onde a chave deveria ser inserida.
//   keyIndex : se found, a posicao da chave dentro de keys[];
//              se !found, a posicao de insercao dentro dessa folha.
//
// O par (nodeIdx, keyIndex) no caso !found e' deliberadamente util: um
// futuro insert pode reaproveitar o ponto de parada da busca.
// ----------------------------------------------------------------------------
template <typename KeyType, int ORDER>
struct SearchResult {
    int  nodeIdx;
    int  keyIndex;
    bool found;
};

#endif // NODE_H