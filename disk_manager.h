#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <string>
#include <fstream>
#include "node.h"

// ============================================================================
//  disk_manager.h  —  DiskIO: a UNICA camada que toca o arquivo .dat.
//
//  Responsabilidades (e SO estas):
//    * abrir/criar o arquivo de dados uma unica vez (stream persistente);
//    * ler e gravar um NodeRecord por indice de slot;
//    * distribuir indices de slot novos (allocateIdx);
//    * contabilizar acessos fisicos a disco (leituras e gravacoes).
//
//  O contador de acessos vive AQUI de proposito: e' este objeto, e nenhum
//  outro, que realmente faz I/O. Cache, LRU e algoritmo de arvore nao
//  contam disco — eles pedem leituras/gravacoes a esta classe.
//
//  Modelo de arquivo: registros de tamanho fixo, enderecados por slot.
//    offset(index) = (index - 1) * sizeof(NodeRecord)
//  Indices comecam em 1 (0 e' a sentinela "sem filho").
//
//  Otimizacao importante: o std::fstream e' aberto UMA vez no construtor e
//  mantido aberto durante toda a vida do objeto. A versao anterior reabria o
//  arquivo a cada leitura/gravacao — uma syscall open()/close() por acesso,
//  que dominava o tempo. Aqui cada acesso e' apenas seek + read/write.
// ============================================================================
template <typename KeyType, int ORDER>
class DiskIO {
public:
    // Abre 'filename' em leitura+escrita binaria, criando-o se nao existir.
    // Calcula o proximo indice livre a partir do tamanho atual do arquivo.
    explicit DiskIO(const std::string& filename);

    // Fecha o stream (o destrutor do fstream ja faz flush do buffer).
    ~DiskIO();

    // Retorna um indice de slot novo e o consome (pos-incremento interno).
    // Nao escreve nada no disco; apenas reserva a "identidade" do no.
    int allocateIdx();

    // Proximo indice que allocateIdx() entregaria (sem consumir).
    int nextFreeIdx() const { return nextIdx_; }

    // Le o slot 'index' para 'rec'. Conta como 1 leitura de disco.
    // Pre-condicao: index >= 1.
    void readRecord(int index, NodeRecord<KeyType, ORDER>& rec);

    // Grava 'rec' no slot 'index'. Conta como 1 gravacao de disco.
    // Pre-condicao: index >= 1.
    void writeRecord(int index, const NodeRecord<KeyType, ORDER>& rec);

    // Forca a descarga do buffer do stream para o sistema de arquivos.
    void sync();

    // Contadores de acesso fisico ao disco.
    int  reads()  const { return reads_;  }
    int  writes() const { return writes_; }
    void resetCounters() { reads_ = 0; writes_ = 0; }

private:
    std::string  filename_;
    std::fstream file_;     // stream persistente (in | out | binary)
    int          nextIdx_;  // proximo slot livre
    int          reads_;    // leituras fisicas acumuladas
    int          writes_;   // gravacoes fisicas acumuladas

    static constexpr std::streamoff kRecordSize =
        static_cast<std::streamoff>(sizeof(NodeRecord<KeyType, ORDER>));
};

#include "disk_manager.tpp"

#endif // DISK_MANAGER_H