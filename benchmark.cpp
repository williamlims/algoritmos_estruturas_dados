// ============================================================================
//  benchmark.cpp  —  Driver UNICO: validacao + benchmarks da B-Tree.
//
//  Fluxo:
//    1) VALIDACAO (oraculo std::set): para cada ordem e cada familia de
//       algoritmo, confere insert/search/remove contra um std::set, com reuso
//       de nos ligado e desligado, e re-verifica apos reabrir o arquivo
//       (persistencia + free-list persistida). Marca cada ordem como VALIDA
//       por familia. A familia preemptiva e' invalida em m=3 (degenera).
//    2) BENCHMARKS: varre ordem x N x padrao(seq/rand) x cache x familia x
//       reuso, medindo por fase (insert/search/delete):
//         - reads/writes de disco (metrica logica principal),
//         - tempo de parede / CPU-usuario / CPU-sistema (getrusage),
//         - ocupacao do arquivo (slots fisicos) e nos vivos (BFS no .dat),
//         - altura da arvore.
//       Emite CSV "tidy" (uma linha por fase) para plotagem em Python.
//
//  Duas FAMILIAS de algoritmo (escolha pareada split+remocao):
//    reactive   : bottom-up, correto para todo m>=3 (PADRAO).
//    preemptive : top-down estilo CLRS, valido so para m>=4.
//
//  Reaproveitamento de nos (free-list): com reuso ligado, slots liberados em
//  fusoes/colapsos da raiz sao reciclados -> o arquivo cresce menos. Comparar
//  reuse=1 x reuse=0 quantifica a economia de ocupacao em disco.
//
//  Nota sobre tempo: wall-(user+sys) ~ espera de dispositivo; com page cache
//  quente fica ~0. Por isso a metrica PRINCIPAL e' o contador LOGICO de I/O.
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

// ---- Familia de algoritmo --------------------------------------------------
enum class Family { Reactive, Preemptive };
const char* familyName(Family f) { return f == Family::Reactive ? "reactive" : "preemptive"; }
SplitPolicy  splitOf(Family f)  { return f == Family::Reactive ? SplitPolicy::Reactive  : SplitPolicy::Preemptive; }
DeletePolicy deleteOf(Family f) { return f == Family::Reactive ? DeletePolicy::Reactive : DeletePolicy::Preemptive; }
// Familia preemptiva degenera em m=3.
bool familyValidForOrder(Family f, int order) { return !(f == Family::Preemptive && order == 3); }

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
    { std::ifstream m(datPath + ".meta"); if (m) m >> root; }   // 1a linha = rootIdx
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

// ===========================================================================
// VALIDACAO contra oraculo std::set
// ===========================================================================
template <int ORDER>
bool validateOne(Family fam, bool reuse, unsigned seed, int ops, int cache) {
    std::string path = "arvores/val_O" + std::to_string(ORDER) + ".dat";
    std::remove(path.c_str()); std::remove((path + ".meta").c_str());
    std::set<int> ref;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> keyDist(1, ops / 2 + 1);
    std::uniform_int_distribution<int> opDist(0, 2);
    bool ok = true;
    {
        BTree<int, ORDER> tree(path, cache, splitOf(fam), deleteOf(fam), reuse);
        for (int s = 0; s < ops && ok; s++) {
            int op = opDist(rng), key = keyDist(rng);
            if (op == 2) { if (tree.remove(key) != (ref.erase(key) > 0)) ok = false; }
            else { if (ref.insert(key).second) tree.insert(key); }
            int pr = keyDist(rng);
            if (ok && tree.search(pr).found != (ref.count(pr) > 0)) ok = false;
        }
        if (ok)
            for (int k = 1; k <= ops / 2 + 1 && ok; k++)
                if (tree.search(k).found != (ref.count(k) > 0)) ok = false;
    }
    if (ok) {   // reabre do disco e revalida (persistencia + free-list)
        BTree<int, ORDER> tree(path, cache, splitOf(fam), deleteOf(fam), reuse);
        for (int k = 1; k <= ops / 2 + 1 && ok; k++)
            if (tree.search(k).found != (ref.count(k) > 0)) ok = false;
    }
    std::remove(path.c_str()); std::remove((path + ".meta").c_str());
    return ok;
}

// ---- Config e CSV ----------------------------------------------------------
struct Cfg {
    int order; long n; std::string pattern; int cache;
    Family family; bool reuse;
    double delFraction; long searchQ; unsigned seed; bool valid; std::string exp;
};

void emitHeader(std::ostream& os) {
    os << "exp,order,n,pattern,cache,family,reuse,valid,phase,ops,"
          "reads,writes,io_total,io_per_op,"
          "wall_s,cpu_user_s,cpu_sys_s,io_wait_s,"
          "record_bytes,file_bytes,slots,live_nodes,height\n";
}

void emitRow(std::ostream& os, const Cfg& c, const std::string& phase,
             long ops, long reads, long writes,
             double wall, double user, double sys,
             long recordBytes, long fileBytes, long slots, long live, long height) {
    long ioTotal = reads + writes;
    double perOp = ops > 0 ? double(ioTotal) / double(ops) : 0.0;
    double wait  = wall - (user + sys); if (wait < 0) wait = 0;
    os << c.exp << ',' << c.order << ',' << c.n << ',' << c.pattern << ','
       << c.cache << ',' << familyName(c.family) << ',' << (c.reuse ? 1 : 0) << ','
       << (c.valid ? 1 : 0) << ',' << phase << ','
       << ops << ',' << reads << ',' << writes << ',' << ioTotal << ',' << perOp << ','
       << wall << ',' << user << ',' << sys << ',' << wait << ','
       << recordBytes << ',' << fileBytes << ',' << slots << ',' << live << ','
       << height << '\n';
}

// ---- Roda uma configuracao completa (insert + search + delete) -------------
template <int ORDER>
void runConfig(std::ostream& os, const Cfg& c) {
    std::string path = "arvores/bench_O" + std::to_string(ORDER) + ".dat";
    std::remove(path.c_str()); std::remove((path + ".meta").c_str());

    std::vector<int> keys(c.n);
    std::iota(keys.begin(), keys.end(), 1);
    std::mt19937 rng(c.seed);
    if (c.pattern == "rand") std::shuffle(keys.begin(), keys.end(), rng);

    long recBytes = static_cast<long>(sizeof(NodeRecord<int, ORDER>));
    SplitPolicy sp = splitOf(c.family); DeletePolicy dp = deleteOf(c.family);

    // ===== INSERT =====
    long insReads = 0, insWrites = 0; double insWall = 0, insUser = 0, insSys = 0;
    {
        BTree<int, ORDER> tree(path, c.cache, sp, dp, c.reuse);
        tree.resetDiskCounters();
        CpuClock clk; clk.start();
        for (int k : keys) tree.insert(k);
        tree.flush();
        clk.stop(insWall, insUser, insSys);
        insReads = tree.diskReads(); insWrites = tree.diskWrites();
    }
    Occupancy occIns = measureOccupancy<ORDER>(path);
    emitRow(os, c, "insert", c.n, insReads, insWrites, insWall, insUser, insSys,
            recBytes, occIns.fileBytes, occIns.slots, occIns.live, occIns.height);

    // ===== SEARCH + DELETE + REINSERT (reabre a arvore persistida) =====
    long seOps = 0, seReads = 0, seWrites = 0; double seWall = 0, seUser = 0, seSys = 0;
    long delOps = 0, delReads = 0, delWrites = 0; double delWall = 0, delUser = 0, delSys = 0;
    long reinsOps = 0, reinsReads = 0, reinsWrites = 0; double reinsWall = 0, reinsUser = 0, reinsSys = 0;
    long ndel = (long)(c.delFraction * c.n);
    {
        BTree<int, ORDER> tree(path, c.cache, sp, dp, c.reuse);

        {   // SEARCH: 50% existentes, 50% ausentes
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

        {   // DELETE: remove uma fracao aleatoria de {1..N}
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

        {   // REINSERT (churn): insere ndel chaves NOVAS (acima do range). Aqui a
            // free-list e' exercitada: com reuso, as alocacoes reusam slots
            // liberados pelas remocoes e o arquivo NAO cresce; sem reuso, cresce.
            std::vector<int> add(ndel);
            for (long i = 0; i < ndel; i++) add[i] = (int)(c.n + 1 + i);
            std::mt19937 arng(c.seed + 123);
            std::shuffle(add.begin(), add.end(), arng);
            tree.resetDiskCounters();
            CpuClock clk; clk.start();
            for (int x : add) tree.insert(x);
            tree.flush();
            clk.stop(reinsWall, reinsUser, reinsSys);
            reinsOps = ndel; reinsReads = tree.diskReads(); reinsWrites = tree.diskWrites();
        }
    }

    emitRow(os, c, "search", seOps, seReads, seWrites, seWall, seUser, seSys,
            recBytes, 0, 0, 0, occIns.height);

    Occupancy occDel = measureOccupancy<ORDER>(path);
    emitRow(os, c, "delete", delOps, delReads, delWrites, delWall, delUser, delSys,
            recBytes, occDel.fileBytes, occDel.slots, occDel.live, occDel.height);

    Occupancy occFin = measureOccupancy<ORDER>(path);
    emitRow(os, c, "reinsert", reinsOps, reinsReads, reinsWrites,
            reinsWall, reinsUser, reinsSys,
            recBytes, occFin.fileBytes, occFin.slots, occFin.live, occFin.height);

    std::remove(path.c_str()); std::remove((path + ".meta").c_str());
}

// ---- Despacho runtime -> template ------------------------------------------
bool dispatch(std::ostream& os, const Cfg& c) {
    switch (c.order) {
        case 3:    runConfig<3>(os, c);    return true;
        case 4:    runConfig<4>(os, c);    return true;
        case 5:    runConfig<5>(os, c);    return true;
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
bool dispatchValidate(int order, Family fam, bool reuse, unsigned seed, int ops, int cache) {
    switch (order) {
        case 3:    return validateOne<3>(fam, reuse, seed, ops, cache);
        case 4:    return validateOne<4>(fam, reuse, seed, ops, cache);
        case 5:    return validateOne<5>(fam, reuse, seed, ops, cache);
        case 8:    return validateOne<8>(fam, reuse, seed, ops, cache);
        case 16:   return validateOne<16>(fam, reuse, seed, ops, cache);
        case 32:   return validateOne<32>(fam, reuse, seed, ops, cache);
        case 64:   return validateOne<64>(fam, reuse, seed, ops, cache);
        case 100:  return validateOne<100>(fam, reuse, seed, ops, cache);
        case 128:  return validateOne<128>(fam, reuse, seed, ops, cache);
        case 256:  return validateOne<256>(fam, reuse, seed, ops, cache);
        case 512:  return validateOne<512>(fam, reuse, seed, ops, cache);
        case 1000: return validateOne<1000>(fam, reuse, seed, ops, cache);
        case 1024: return validateOne<1024>(fam, reuse, seed, ops, cache);
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
std::vector<Family> parseFamilies(const std::string& s) {
    std::vector<Family> v;
    for (auto& t : parseStrList(s)) {
        if (t == "reactive")        v.push_back(Family::Reactive);
        else if (t == "preemptive") v.push_back(Family::Preemptive);
        else { std::cerr << "familia desconhecida: " << t << "\n"; std::exit(2); }
    }
    return v;
}

// ===========================================================================
// FASE 1: validacao (sempre, salvo --no-validate)
// ===========================================================================
std::map<std::pair<int,int>, bool> runValidation(const std::vector<long>& orders,
                                                 const std::vector<Family>& families,
                                                 const std::vector<int>& reuses,
                                                 unsigned seed, const std::string& outDir) {
    std::cerr << "==== VALIDACAO (oraculo std::set) ====\n";
    std::ofstream vcsv(outDir + "/validity.csv");
    vcsv << "order,family,reuse,seed,result\n";
    std::map<std::pair<int,int>, bool> validOf;  // (order, family) -> valido
    bool allOk = true;
    for (long o : orders) {
        for (Family fam : families) {
            int fkey = (fam == Family::Reactive ? 0 : 1);
            if (!familyValidForOrder(fam, (int)o)) {
                validOf[{(int)o, fkey}] = false;
                std::cerr << "  [m=" << o << "] " << familyName(fam)
                          << "  -> N/A (degenera em m=3)\n";
                vcsv << o << ',' << familyName(fam) << ",-,-," << "na\n";
                continue;
            }
            bool ok = true;
            for (int reuse : reuses)
                for (unsigned s = seed; s < seed + 3 && ok; s++) {
                    bool r = dispatchValidate((int)o, fam, reuse != 0, s, 6000, 3);
                    vcsv << o << ',' << familyName(fam) << ',' << reuse << ',' << s
                         << ',' << (r ? "ok" : "FALHA") << '\n';
                    ok = ok && r;
                }
            validOf[{(int)o, fkey}] = ok;
            allOk = allOk && ok;
            std::cerr << "  [m=" << o << "] " << familyName(fam)
                      << "  reuse{" ;
            for (size_t i=0;i<reuses.size();i++) std::cerr << reuses[i] << (i+1<reuses.size()?",":"");
            std::cerr << "}  -> " << (ok ? "VALIDA" : "FALHA") << "\n";
        }
    }
    std::cerr << (allOk ? ">> validacao OK\n" : ">> VALIDACAO FALHOU\n");
    return validOf;
}

// ===========================================================================
// FASE 2: varredura de benchmark
// ===========================================================================
void runSweep(std::ostream& os, const std::string& exp,
              const std::vector<long>& orders, const std::vector<long>& sizes,
              const std::vector<std::string>& patterns, const std::vector<long>& caches,
              const std::vector<Family>& families, const std::vector<int>& reuses,
              double delFraction, long searchQ, unsigned seed,
              const std::map<std::pair<int,int>, bool>& validOf) {
    long total = (long)orders.size()*sizes.size()*patterns.size()*caches.size()
                 *families.size()*reuses.size();
    long done = 0;
    for (long o : orders)
      for (Family fam : families) {
        if (!familyValidForOrder(fam, (int)o)) { done += sizes.size()*patterns.size()*caches.size()*reuses.size(); continue; }
        int fkey = (fam == Family::Reactive ? 0 : 1);
        bool valid = true;
        auto it = validOf.find({(int)o, fkey});
        if (it != validOf.end()) valid = it->second;
        for (long n : sizes)
          for (const auto& p : patterns)
            for (long cache : caches)
              for (int reuse : reuses) {
                Cfg c; c.order=(int)o; c.n=n; c.pattern=p; c.cache=(int)cache;
                c.family=fam; c.reuse=(reuse!=0);
                c.delFraction=delFraction; c.searchQ=std::min<long>(searchQ, n*2);
                c.seed=seed; c.valid=valid; c.exp=exp;
                std::cerr << "[" << exp << " " << (++done) << "/" << total << "] m=" << o
                          << " n=" << n << " " << p << " cache=" << cache
                          << " " << familyName(fam) << " reuse=" << reuse
                          << (valid ? "" : " (INVALIDA)") << "\n";
                dispatch(os, c);
                os.flush();
            }
      }
}

} // namespace

int main(int argc, char** argv) {
    // Defaults (rapidos). Para o estudo final, escale via flags (ex.: N ate 1e6).
    std::vector<long>        orders   = {3, 4, 8, 16, 32, 64, 128, 256};
    std::vector<long>        sizes    = {1000, 10000, 100000};
    std::vector<std::string> patterns = {"seq", "rand"};
    std::vector<long>        caches   = {3};
    std::vector<Family>      families = {Family::Reactive};
    std::vector<int>         reuses   = {1};
    double   delFraction = 0.5;
    long     searchQ     = 20000;
    unsigned seed        = 42;
    std::string outPath  = "out/results.csv";
    std::string expName  = "order_sweep";
    bool runAll = true;          // suite curada por padrao
    bool doValidate = true;
    bool sweepFlagsGiven = false;

    for (int i = 1; i < argc; i++) {
        std::string f = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "falta valor para " << f << "\n"; std::exit(2); }
            return argv[++i];
        };
        if      (f == "--orders")   { orders=parseList(next());        sweepFlagsGiven=true; }
        else if (f == "--sizes")    { sizes=parseList(next());         sweepFlagsGiven=true; }
        else if (f == "--patterns") { patterns=parseStrList(next());   sweepFlagsGiven=true; }
        else if (f == "--caches")   { caches=parseList(next());        sweepFlagsGiven=true; }
        else if (f == "--families") { families=parseFamilies(next());  sweepFlagsGiven=true; }
        else if (f == "--reuse")    { reuses=[&]{auto l=parseList(next());std::vector<int>r;for(long x:l)r.push_back((int)x);return r;}(); sweepFlagsGiven=true; }
        else if (f == "--del-frac") delFraction = std::stod(next());
        else if (f == "--search-q") searchQ  = std::stol(next());
        else if (f == "--seed")     seed     = (unsigned)std::stol(next());
        else if (f == "--out")      outPath  = next();
        else if (f == "--exp")      { expName = next(); sweepFlagsGiven = true; }
        else if (f == "--no-validate") doValidate = false;
        else if (f == "--only-validate") { runAll = false; sweepFlagsGiven = false; }
        else { std::cerr << "flag desconhecida: " << f << "\n"; std::exit(2); }
    }
    if (sweepFlagsGiven) runAll = false;   // flags explicitas -> uma varredura

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories("arvores", ec);
    fs::path op(outPath);
    std::string outDir = op.has_parent_path() ? op.parent_path().string() : ".";
    fs::create_directories(outDir, ec);

    // ---- FASE 1: validacao ----
    std::map<std::pair<int,int>, bool> validOf;
    if (doValidate) {
        // valida em todas as ordens que serao usadas, nas duas familias, reuso on/off
        std::vector<long> valOrders = {3,4,5,8,16,32,64,128,256};
        std::vector<Family> valFams = {Family::Reactive, Family::Preemptive};
        std::vector<int> valReuse = {1,0};
        validOf = runValidation(valOrders, valFams, valReuse, seed, outDir);
    }

    bool onlyValidate = (!runAll && !sweepFlagsGiven);
    if (onlyValidate) { std::cerr << "(--only-validate) sem benchmarks.\n"; return 0; }

    std::ofstream os(outPath);
    if (!os) { std::cerr << "nao abriu " << outPath << "\n"; return 1; }
    emitHeader(os);

    if (runAll) {
        // ---- Suite curada (defaults modestos; escale com flags p/ o estudo final) ----
        // 1) Varredura de ORDEM (familia reativa, reuso on): altura e I/O vs m.
        runSweep(os, "order_sweep", {3,4,8,16,32,64,128,256}, {100000},
                 {"seq","rand"}, {3}, {Family::Reactive}, {1},
                 delFraction, searchQ, seed, validOf);
        // 2) Varredura de N (poucas ordens): escalabilidade 1e3..1e5 (mude p/ 1e6).
        runSweep(os, "size_sweep", {8,100}, {1000,10000,100000},
                 {"seq","rand"}, {3}, {Family::Reactive}, {1},
                 delFraction, searchQ, seed, validOf);
        // 3) Comparacao de FAMILIAS (preemptiva x reativa), m>=4.
        runSweep(os, "family_compare", {4,8,16,32,128}, {100000},
                 {"seq","rand"}, {3}, {Family::Reactive, Family::Preemptive}, {1},
                 delFraction, searchQ, seed, validOf);
        // 4) Reaproveitamento de nos: reuso on x off (ocupacao apos remocoes).
        runSweep(os, "reuse_compare", {3,4,8,32,100}, {100000},
                 {"rand"}, {3}, {Family::Reactive}, {1,0},
                 delFraction, searchQ, seed, validOf);
    } else {
        runSweep(os, expName, orders, sizes, patterns, caches, families, reuses,
                 delFraction, searchQ, seed, validOf);
    }

    std::cerr << "OK -> " << outPath << "\n";
    return 0;
}
