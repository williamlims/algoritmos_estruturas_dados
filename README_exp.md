# Benchmark da B-Tree — guia de compilação e execução

Estrutura usada de verdade no benchmark: os mesmos `btree.h/.tpp`,
`buffer_pool.h/.tpp`, `disk_manager.h/.tpp` e `node.h` que você enviou.
O driver `benchmark.cpp` apenas os instancia, mede e emite CSV; a plotagem
fica em `plots.py` (matplotlib), porque toda a parte de desempenho/algoritmo
roda em C++ e só os números agregados vão para o Python.

## 0. Layout

```
src/   btree.h btree.tpp buffer_pool.h buffer_pool.tpp
       disk_manager.h disk_manager.tpp node.h
       benchmark.cpp        <- driver novo
arvores/  (arquivos .dat temporarios; criados e apagados pelo benchmark)
out/      results.csv, results_cache.csv, figs/, tabelas.md
plots.py
```

## 1. Compilação

Os `.tpp` são **incluídos** pelos headers (no fim de cada `.h`), então NÃO se
compila `.tpp` separadamente — basta compilar o `.cpp` com os headers no
include path. Cada arquivo com `main()` vira um binário próprio.

```bash
cd src

# Driver de benchmark (instancia varias ordens num so binario)
g++ -O2 -std=c++17 benchmark.cpp -o ../benchmark

# Seus arquivos originais (cada um tem seu main):
g++ -O2 -std=c++17 main.cpp        -o ../main
g++ -O2 -std=c++17 test_btree.cpp  -o ../test_btree
# bench.cpp precisa da ordem em tempo de compilacao (template):
g++ -O2 -std=c++17 -DBENCH_ORDER=4   bench.cpp -o ../bench_o4
g++ -O2 -std=c++17 -DBENCH_ORDER=64  bench.cpp -o ../bench_o64
```

> Por que `benchmark.cpp` e não recompilar `bench.cpp` por ordem? `ORDER` é
> parâmetro de *template* (constante de compilação). Para varrer dezenas de
> ordens, o `benchmark.cpp` instancia todas e despacha em runtime por um
> `switch` (`dispatch()`), evitando dezenas de binários. Para acrescentar uma
> ordem nova, adicione um `case N:` em `dispatch()` **e** em `dispatchProbe()`.

## 2. Execução dos experimentos

Tudo a partir da raiz do projeto (a pasta que contém `benchmark` e `arvores/`).

```bash
# EXP A — varredura de ORDEM (ordens validas, faixa 10^3..10^6, seq e rand)
./benchmark --exp order_sweep \
  --orders 4,8,16,32,64,128,256,512 \
  --sizes 1000,10000,100000,1000000 \
  --patterns seq,rand --caches 3 --search-q 20000 --seed 42 \
  --out out/A_valid.csv

# EXP A (m=3) — documenta a degeneracao; so tamanhos pequenos
./benchmark --exp order_sweep --orders 3 --sizes 1000,10000 \
  --patterns seq,rand --caches 3 --out out/A_m3.csv

# EXP B — varredura de CACHE (ordem e N fixos)
./benchmark --exp cache_sweep --orders 32 --sizes 100000 --patterns rand \
  --caches 2,4,8,16,32,64,128,256,512,1024 --out out/B_cache.csv

# Consolida para o plots.py
head -1 out/A_valid.csv  > out/results.csv
tail -n +2 out/A_valid.csv >> out/results.csv
tail -n +2 out/A_m3.csv    >> out/results.csv
cp out/B_cache.csv out/results_cache.csv
```

### Flags do benchmark
| flag | significado | padrão |
|---|---|---|
| `--orders`   | lista de ordens m (vírgula) | `3,4,8,16,32,64,128,256` |
| `--sizes`    | lista de N (vírgula) | `1000,10000,100000` |
| `--patterns` | `seq` e/ou `rand` (ordem de inserção) | `seq,rand` |
| `--caches`   | tamanhos do buffer pool | `3` |
| `--del-frac` | fração de chaves removidas na fase delete | `0.5` |
| `--search-q` | nº de buscas (limitado a 2·N) | `20000` |
| `--seed`     | semente RNG | `42` |
| `--out`      | caminho do CSV | `out/results.csv` |
| `--exp`      | rótulo do experimento | `order_sweep` |

Ordens instanciadas: 3, 4, 8, 16, 32, 64, 100, 128, 256, 512, 1000, 1024.

## 3. Gráficos

```bash
pip install matplotlib pandas tabulate   # tabulate so p/ tabelas.md
python3 plots.py
# -> out/figs/*.png  e  out/tabelas.md
```

## 4. O que cada coluna do CSV significa
Uma linha por (config, fase). `phase ∈ {insert, search, delete}`.

- `reads`,`writes` — acessos **lógicos** ao disco contados pelo `DiskIO`
  (miss de cache / gravação real). É a métrica central, independente do
  page cache do SO.
- `io_total`,`io_per_op` — soma e média por operação da fase.
- `wall_s`,`cpu_user_s`,`cpu_sys_s`,`io_wait_s` — `chrono` + `getrusage`.
  `io_wait = wall − (user+sys)`. Como o `DiskIO` faz `flush()` por gravação,
  o custo de I/O aparece sobretudo como **CPU-sistema** (syscalls); a espera
  de dispositivo fica ~0 com page cache quente.
- `slots` — nós alocados no arquivo (`tamanho/recordSize`); **nunca encolhe**.
- `live_nodes` — nós alcançáveis a partir da raiz (lidos do `.dat` cru).
- `bytes_no_reuse = slots·recordSize` (arquivo real, sem free-list).
- `bytes_reuse   = live_nodes·recordSize` (ideal, se houvesse reaproveitamento).
- `height` — número de níveis da árvore (medido pós-inserção).
- `valid` — 1 se a sonda de correção (BTree × std::set) passou para a ordem.

## 5. Notas de metodologia / limitações
- **m = 3 é inválida nesta implementação**: no `splitChild`,
  `newN = ORDER−2−mid = 0`, gerando nó-irmão com 0 chaves. A sonda marca
  `valid=0` e os gráficos de tendência filtram `valid==1`. Use m ≥ 4.
- O conjunto de chaves é sempre `{1..N}`; muda apenas a **ordem** de inserção
  (`seq` = crescente, `rand` = embaralhada). Isso isola o efeito do padrão.
- Reúso de nós: a implementação **não** tem free-list, então `bytes_reuse` é
  um limite inferior teórico (compactação ideal), não uma segunda execução.
  Para medir reúso real, seria preciso adicionar uma free-list ao `DiskIO`
  (`allocateIdx` consumindo slots liberados em merge/colapso de raiz).
