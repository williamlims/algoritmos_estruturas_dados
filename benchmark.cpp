// ============================================================================
//  benchmark.cpp  —  Driver unico de benchmark da B-Tree.
//
//  Diferente do bench.cpp original (que recompila por ordem via -DBENCH_ORDER),
//  este binario instancia a BTree para VARIAS ordens de uma vez e despacha em
//  runtime por um switch. Um unico executavel varre todo o espaco de parametros
//  e emite CSV "tidy" (uma linha por fase) para o pandas/matplotlib.
//
//  Mede, por configuracao (ordem m, tamanho N, padrao de insercao, cache):
//    * INSERT : reads/writes de disco, tempo (wall / CPU-user / CPU-sys),
//               e a ocupacao do arquivo logo apos a construcao.
//    * SEARCH : reads/writes e tempo de M buscas (50% existentes / 50% nao).
//    * DELETE : reads/writes e tempo ao remover uma fracao das chaves,
//               e a ocupacao do arquivo APOS as remocoes (orfaos surgem).
//
//  Ocupacao: o DiskIO nao tem free-list -> slots = arquivo/recordSize nunca
//  encolhe. Contamos os nos VIVOS (alcancaveis da raiz, lendo o .dat cru) e
//  derivamos:
//      bytes_sem_reuso = slots_alocados * recordSize  (= tamanho real do .dat)
//      bytes_com_reuso = nos_vivos       * recordSize  (compactacao ideal)
//  A diferenca = fragmentacao por nos orfaos, visivel apos remocoes.
//
//  Tempo: chrono = relogio de parede; getrusage separa CPU-usuario (algoritmo)
//  de CPU-sistema (syscalls de I/O — o DiskIO faz flush por gravacao).
//  wall-(user+sys) ~ espera real de dispositivo (~0 com page cache quente);
//  por isso a metrica PRINCIPAL e' o contador LOGICO reads/writes.
// ============================================================================

#include "btree.h"
#include "node.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <sys/resource.h>   // getrusage

namespace {

// ---- Relogio: parede + CPU usuario + CPU sistema --------------------------
struct CpuClock {
    std::chrono::steady_clock::time_point w0;
    rusage r0{};
    void start() { getrusage(RUSAGE_SELF, &r0); w0 = std::chrono::steady_clock::now(); }
    static double tv(const timeval& a, const timeval& b) {
        return (a.tv_sec - b.tv_sec) + (a.tv_usec - b.tv_usec) / 1e6;
    }
    void stop(double& wall, double& user, double& sys) {
        auto w1 = std::chrono::steady_clock::now();
        rusage r1{}; getrusage(RUSAGE_SELF, &r1);
        wall = std::chrono::duration<double>(w1 - w0).count();
        user = tv(r1.ru_utime, r0.ru_utime);
        sys  = tv(r1.ru_stime, r0.ru_stime);
    }
};

// ---- Ocupacao: le o .dat cru, BFS a partir da raiz (do .meta) -------------
struct Occupancy { long slots = 0, live = 0, height = 0, recordSize = 0, fileBytes = 0; };

template <int ORDER>
Occupancy measureOccupancy(const std::string& datPath) {
    using Rec = NodeRecord<int, ORDER>;
    Occupancy occ;
    occ.recordSize = static_cast<long>(sizeof(Rec));

    std::ifstream f(datPath, std::ios::binary | std::ios::ate);
    if (!f) return occ;
    occ.fileBytes = static_cast<long>(f.tellg());
    occ.slots     = occ.recordSize ? occ.fileBytes / occ.recordSize : 0;

    int root = 0;
    { std::ifstream m(datPath + ".meta"); if (m) m >> root; }
    if (root <= 0) return occ;

    auto readRec = [&](int idx, Rec& rec) -> bool {
        std::streamoff off = (static_cast<std::streamoff>(idx) - 1) * occ.recordSize;
        f.clear(); f.seekg(off);
        return static_cast<bool>(f.read(reinterpret_cast<char*>(&rec), sizeof(Rec)));
    };

    std::vector<int> cur{root}, nxt;
    long levels = 0;
    while (!cur.empty()) {
        levels++;
        for (int idx : cur) {
            Rec rec;
            if (!readRec(idx, rec)) continue;
            occ.live++;
            if (rec.filhoIdx[0] != 0)
                for (int c = 0; c <= rec.n; c++)
                    if (rec.filhoIdx[c] != 0) nxt.push_back(rec.filhoIdx[c]);
        }
        cur.swap(nxt); nxt.clear();
    }
    occ.height = levels;
    return occ;
}

// ---- Sonda de correcao rapida (BTree vs std::set). Pega o m=3 degenerado ---
template <int ORDER>
bool correctnessProbe(unsigned seed, int ops, int cache) {
    std::string path = "arvores/probe_O" + std::to_string(ORDER) + ".dat";
    std::remove(path.c_str()); std::remove((path + ".meta").c_str());
    std::set<int> oracle;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> keyDist(1, ops / 2 + 1);
    std::uniform_int_distribution<int> opDist(0, 2);
    bool ok = true;
    {
        BTree<int, ORDER> tree(path, cache);
        for (int s = 0; s < ops && ok; s++) {
            int op = opDist(rng), key = keyDist(rng);
            if (op == 2) { if (tree.remove(key) != (oracle.erase(key) > 0)) ok = false; }
            else { if (oracle.insert(key).second) tree.insert(key); }
            int pr = keyDist(rng);
            if (tree.search(pr).found != (oracle.count(pr) > 0)) ok = false;
        }
    }
    std::remove(path.c_str()); std::remove((path + ".meta").c_str());
    return ok;
}

// ---- Config e CSV ----------------------------------------------------------
struct Cfg {
    int order; long n; std::string pattern; int cache;
    double delFraction; long searchQ; unsigned seed; bool valid; std::string exp;
};

void emitHeader(std::ostream& os) {
    os << "exp,order,n,pattern,cache,valid,phase,ops,"
          "reads,writes,io_total,io_per_op,"
          "wall_s,cpu_user_s,cpu_sys_s,io_wait_s,"
          "record_bytes,file_bytes,slots,live_nodes,"
          "bytes_no_reuse,bytes_reuse,height\n";
}

void emitRow(std::ostream& os, const Cfg& c, const std::string& phase,
             long ops, long reads, long writes,
             double wall, double user, double sys,
             long recordBytes, long fileBytes, long slots, long live, long height) {
    long ioTotal = reads + writes;
    double perOp = ops > 0 ? double(ioTotal) / double(ops) : 0.0;
    double wait  = wall - (user + sys); if (wait < 0) wait = 0;
    os << c.exp << ',' << c.order << ',' << c.n << ',' << c.pattern << ','
       << c.cache << ',' << (c.valid ? 1 : 0) << ',' << phase << ','
       << ops << ',' << reads << ',' << writes << ',' << ioTotal << ',' << perOp << ','
       << wall << ',' << user << ',' << sys << ',' << wait << ','
       << recordBytes << ',' << fileBytes << ',' << slots << ',' << live << ','
       << (slots * recordBytes) << ',' << (live * recordBytes) << ',' << height << '\n';
}

// ---- Roda uma configuracao completa (insert + search + delete) -------------
template <int ORDER>
void runConfig(std::ostream& os, const Cfg& c) {
    std::string path = "arvores/bench_O" + std::to_string(ORDER) + ".dat";
    std::remove(path.c_str()); std::remove((path + ".meta").c_str());

    // Conjunto = {1..N} nos dois padroes; muda so a ORDEM de insercao.
    std::vector<int> keys(c.n);
    std::iota(keys.begin(), keys.end(), 1);
    std::mt19937 rng(c.seed);
    if (c.pattern == "rand") std::shuffle(keys.begin(), keys.end(), rng);

    long recBytes = static_cast<long>(sizeof(NodeRecord<int, ORDER>));

    // ===== INSERT =====
    long insReads = 0, insWrites = 0; double insWall = 0, insUser = 0, insSys = 0;
    {
        BTree<int, ORDER> tree(path, c.cache);
        tree.resetDiskCounters();
        CpuClock clk; clk.start();
        for (int k : keys) tree.insert(k);
        tree.flush();
        clk.stop(insWall, insUser, insSys);
        insReads = tree.diskReads(); insWrites = tree.diskWrites();
    } // fecha -> persiste .meta
    Occupancy occIns = measureOccupancy<ORDER>(path);
    emitRow(os, c, "insert", c.n, insReads, insWrites, insWall, insUser, insSys,
            recBytes, occIns.fileBytes, occIns.slots, occIns.live, occIns.height);

    // ===== SEARCH + DELETE (reabre a arvore persistida) =====
    long seOps = 0, seReads = 0, seWrites = 0; double seWall = 0, seUser = 0, seSys = 0;
    long delOps = 0, delReads = 0, delWrites = 0; double delWall = 0, delUser = 0, delSys = 0;
    {
        BTree<int, ORDER> tree(path, c.cache);

        // --- SEARCH: 50% existentes (1..N), 50% ausentes (>N) ---
        {
            std::mt19937 qrng(c.seed + 7);
            std::uniform_int_distribution<int> hit(1, (int)c.n);
            std::uniform_int_distribution<int> miss((int)c.n + 1, (int)c.n + 1000000);
            std::vector<int> q; q.reserve(c.searchQ);
            for (long i = 0; i < c.searchQ; i++)
                q.push_back(i % 2 == 0 ? hit(qrng) : miss(qrng));
            tree.resetDiskCounters();
            CpuClock clk; clk.start();
            volatile long found = 0;
            for (int x : q) if (tree.search(x).found) found++;
            clk.stop(seWall, seUser, seSys); (void)found;
            seOps = (long)q.size(); seReads = tree.diskReads(); seWrites = tree.diskWrites();
        }

        // --- DELETE: remove uma fracao aleatoria de {1..N} ---
        {
            long ndel = (long)(c.delFraction * c.n);
            std::vector<int> del(c.n);
            std::iota(del.begin(), del.end(), 1);
            std::mt19937 drng(c.seed + 99);
            std::shuffle(del.begin(), del.end(), drng);
            del.resize(ndel);
            tree.resetDiskCounters();
            CpuClock clk; clk.start();
            volatile long removed = 0;
            for (int x : del) if (tree.remove(x)) removed++;
            tree.flush();
            clk.stop(delWall, delUser, delSys); (void)removed;
            delOps = ndel; delReads = tree.diskReads(); delWrites = tree.diskWrites();
        }
    } // fecha -> persiste .meta pos-delete

    emitRow(os, c, "search", seOps, seReads, seWrites, seWall, seUser, seSys,
            recBytes, 0, 0, 0, occIns.height);

    Occupancy occDel = measureOccupancy<ORDER>(path);
    emitRow(os, c, "delete", delOps, delReads, delWrites, delWall, delUser, delSys,
            recBytes, occDel.fileBytes, occDel.slots, occDel.live, occDel.height);

    std::remove(path.c_str()); std::remove((path + ".meta").c_str());
}

// ---- Despacho runtime -> template ------------------------------------------
bool dispatch(std::ostream& os, const Cfg& c) {
    switch (c.order) {
        case 3:    runConfig<3>(os, c);    return true;
        case 4:    runConfig<4>(os, c);    return true;
        case 8:    runConfig<8>(os, c);    return true;
        case 16:   runConfig<16>(os, c);   return true;
        case 32:   runConfig<32>(os, c);   return true;
        case 64:   runConfig<64>(os, c);   return true;
        case 100:  runConfig<100>(os, c);  return true;
        case 128:  runConfig<128>(os, c);  return true;
        case 256:  runConfig<256>(os, c);  return true;
        case 512:  runConfig<512>(os, c);  return true;
        case 1000: runConfig<1000>(os, c); return true;
        case 1024: runConfig<1024>(os, c); return true;
        default: std::cerr << "ordem nao instanciada: " << c.order << "\n"; return false;
    }
}
bool dispatchProbe(int order, unsigned seed, int ops, int cache) {
    switch (order) {
        case 3:    return correctnessProbe<3>(seed, ops, cache);
        case 4:    return correctnessProbe<4>(seed, ops, cache);
        case 8:    return correctnessProbe<8>(seed, ops, cache);
        case 16:   return correctnessProbe<16>(seed, ops, cache);
        case 32:   return correctnessProbe<32>(seed, ops, cache);
        case 64:   return correctnessProbe<64>(seed, ops, cache);
        case 100:  return correctnessProbe<100>(seed, ops, cache);
        case 128:  return correctnessProbe<128>(seed, ops, cache);
        case 256:  return correctnessProbe<256>(seed, ops, cache);
        case 512:  return correctnessProbe<512>(seed, ops, cache);
        case 1000: return correctnessProbe<1000>(seed, ops, cache);
        case 1024: return correctnessProbe<1024>(seed, ops, cache);
        default:   return false;
    }
}

std::vector<long> parseList(const std::string& s) {
    std::vector<long> v; std::stringstream ss(s); std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(std::stol(t));
    return v;
}
std::vector<std::string> parseStrList(const std::string& s) {
    std::vector<std::string> v; std::stringstream ss(s); std::string t;
    while (std::getline(ss, t, ',')) if (!t.empty()) v.push_back(t);
    return v;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<long>        orders   = {3, 4, 8, 16, 32, 64, 128, 256};
    std::vector<long>        sizes    = {1000, 10000, 100000};
    std::vector<std::string> patterns = {"seq", "rand"};
    std::vector<long>        caches   = {3};
    double   delFraction = 0.5;
    long     searchQ     = 20000;
    unsigned seed        = 42;
    std::string outPath  = "out/results.csv";
    std::string expName  = "order_sweep";

    for (int i = 1; i < argc; i++) {
        std::string f = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "falta valor para " << f << "\n"; std::exit(2); }
            return argv[++i];
        };
        if      (f == "--orders")   orders   = parseList(next());
        else if (f == "--sizes")    sizes    = parseList(next());
        else if (f == "--patterns") patterns = parseStrList(next());
        else if (f == "--caches")   caches   = parseList(next());
        else if (f == "--del-frac") delFraction = std::stod(next());
        else if (f == "--search-q") searchQ  = std::stol(next());
        else if (f == "--seed")     seed     = (unsigned)std::stol(next());
        else if (f == "--out")      outPath  = next();
        else if (f == "--exp")      expName  = next();
        else { std::cerr << "flag desconhecida: " << f << "\n"; std::exit(2); }
    }

    // Garante que as pastas existam (senao a criacao do .dat falha em silencio,
    // as leituras devolvem registros com 'n' lixo e o laco varre fora dos
    // limites -> segfault). Cria "arvores/" (onde vivem os .dat) e a pasta do CSV.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("arvores", ec);
    { fs::path op(outPath); if (op.has_parent_path()) fs::create_directories(op.parent_path(), ec); }

    std::ofstream os(outPath);
    if (!os) { std::cerr << "nao abriu " << outPath << "\n"; return 1; }
    emitHeader(os);

    std::map<long, bool> validOf;
    for (long o : orders) {
        bool v = dispatchProbe((int)o, seed, 4000, 4);
        validOf[o] = v;
        std::cerr << "[probe] ordem " << o << ": " << (v ? "VALIDA" : "INVALIDA") << "\n";
    }

    long total = (long)orders.size() * sizes.size() * patterns.size() * caches.size();
    long done = 0;
    for (long o : orders)
        for (long n : sizes)
            for (const auto& p : patterns)
                for (long cache : caches) {
                    Cfg c;
                    c.order = (int)o; c.n = n; c.pattern = p; c.cache = (int)cache;
                    c.delFraction = delFraction;
                    c.searchQ = std::min<long>(searchQ, n * 2);
                    c.seed = seed; c.valid = validOf[o]; c.exp = expName;
                    std::cerr << "[run " << (++done) << "/" << total << "] order=" << o
                              << " n=" << n << " pattern=" << p << " cache=" << cache
                              << (c.valid ? "" : "  (INVALIDA)") << "\n";
                    dispatch(os, c);
                    os.flush();
                }
    std::cerr << "OK -> " << outPath << "\n";
    return 0;
}