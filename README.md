# Árvore B em memória secundária (padrão SGBD)

Projeto da disciplina de mestrado *Algoritmos e Estruturas de Dados* (5955001-3,
Prof. Dr. José Augusto Baranauskas). Implementa uma classe Árvore B de ordem `m`
projetada para operar **estritamente em memória secundária**: toda interação com
o arquivo acontece um nó por vez, nunca carregando a árvore inteira (nem em
blocos) para a memória principal.

**Equipe:** Antonio Pilan · Thiago Martins · William Lima

---

## 1. Visão geral

A árvore vive em um arquivo binário `.dat` de registros de tamanho fixo, um
registro por nó, endereçado por slot. As operações carregam **apenas o nó
necessário** do disco para a RAM, operam sobre ele e o devolvem ao disco. Para
amortizar acessos repetidos a um mesmo nó durante uma operação (por exemplo a
espinha raiz→folha de uma descida), há um **buffer pool** com cache LRU e
pinagem — a mesma estratégia *lazy load* usada em SGBDs, adaptada aos critérios
do trabalho.

O número de nós residentes em memória é limitado pela capacidade do buffer pool
(padrão 3 — o suficiente para `pai, nó, filho`), e nunca pela quantidade de
chaves: uma base com milhões de chaves gera uma árvore que jamais é carregada
por inteiro. Cada acesso físico (leitura de slot no cache miss, ou gravação) é
contabilizado pela camada de disco — essa é a métrica central do estudo.

Sempre que um `.dat` é criado, mantém-se ao lado um `.dat.meta` que registra
**o endereço da raiz** e a **free-list** de slots reaproveitáveis (ver §4), de
modo que a árvore é recuperada ao reabrir o arquivo.

### Onde fica a raiz? (PPT, slide 2)

A raiz **não** fica fixa em um slot. O índice do slot-raiz é guardado no arquivo
`.meta` (primeira linha). Isso é necessário porque a raiz muda: cresce para um
slot novo quando a árvore aumenta de altura e migra para o único filho quando a
raiz esvazia (colapso na remoção). Ao abrir a árvore, lê-se o `rootIdx` do
`.meta`; durante a execução ele é mantido em memória e reescrito no `.meta` ao
fechar.

---

## 2. Arquitetura (camadas)

A responsabilidade é separada em camadas; cada uma só conhece a de baixo.

| Arquivo | Papel |
|---|---|
| `node.h` | Estruturas de dados puras: `BTreeNode` (nó em RAM), `NodeRecord` (imagem do nó em disco, POD de tamanho fixo) e `SearchResult`. Índice de slot 0 = sentinela "sem filho"; válidos começam em 1. |
| `disk_manager.h` / `.tpp` | `DiskIO`: **única** camada que toca o `.dat`. Abre o arquivo uma vez (stream persistente), lê/grava um registro por slot, distribui slots novos (`allocateIdx`) e **conta os acessos físicos** (`reads`/`writes`). |
| `buffer_pool.h` / `.tpp` | `BufferPool`: cache LRU + pinagem sobre o `DiskIO`. Traduz `BTreeNode` ↔ `NodeRecord`, faz writeback só de nós *dirty*, e expõe `allocate` (slot novo), `allocateAt` (reaproveita slot existente) e `evict` (descarta do cache sem gravar). |
| `btree.h` / `.tpp` | `BTree`: o algoritmo (busca, inserção, remoção, impressão hierárquica). Mantém `rootIdx` e a free-list, persistidos no `.meta`. Estratégias de split e de remoção selecionáveis (§3) e reaproveitamento de nós (§4). |
| `benchmark.cpp` | Driver único: roda a **validação** (oráculo `std::set`) e os **benchmarks**, emitindo CSV. |
| `plot_results.py` | Gera os gráficos a partir do CSV (matplotlib). |
| `main.cpp` | Exemplo mínimo de uso interativo da classe. |

> Observação: a versão anterior tinha um `btree_manager.h` monolítico que
> duplicava o algoritmo; ele era código morto e foi **removido**. As estruturas
> de dados ficam em `node.h` (não em `btree.h`, como dizia o README antigo).
> A validação de correção, que antes vivia em `test_btree.cpp`/`bench.cpp`,
> agora está embutida no `benchmark.cpp`.

---

## 3. Duas famílias de algoritmo (split + remoção)

Inserção e remoção são escolhas **pareadas** — duas faces da mesma estratégia —
selecionáveis em tempo de execução pelo construtor:

```cpp
BTree<int, ORDER> arvore(caminho, /*cache*/3,
                         SplitPolicy::Reactive,    // ou Preemptive
                         DeletePolicy::Reactive,   // ou Preemptive
                         /*reuseNodes*/true);
```

- **Reativa** (bottom-up, *two-pass*, "convencional") — **padrão**.
  Na inserção, desce até a folha sem dividir e deixa o split **subir** se o nó
  transbordar. Na remoção, remove na folha (trocando chave interna pelo seu
  predecessor) e conserta o *underflow* **na volta** da recursão. Ocupação
  mínima padrão `ceil(m/2)−1 = (m−1)/2`. **Vale para todo `m ≥ 3`.**

- **Preemptiva** (top-down, *single-pass*, estilo CLRS).
  Na inserção, divide qualquer nó cheio **na descida**, antes de entrar nele;
  na remoção, faz empréstimo/fusão **na descida**. Faz tudo em uma passada,
  nunca revisita o pai. Ocupação mínima relaxada `(m−2)/2`.
  **Válida apenas para `m ≥ 4`.**

### Por que `m = 3` só funciona na família reativa

A família preemptiva age *proativamente* sobre nós que estão no mínimo, o que
exige `2·min + 1 ≤ m−1`, isto é `min ≤ (m−2)/2` — que **zera em `m = 3`**. Com
mínimo 0, o split gera um nó-irmão de 0 chaves e a fusão admite nó de 0 chaves:
a estrutura degenera. A família reativa age *sob demanda* sobre nós já
deficientes, exigindo apenas `2·min ≤ m−1`, satisfeito pelo mínimo padrão
`(m−1)/2` mesmo em `m = 3`. Por isso a inserção preemptiva é exatamente o que
quebra `m = 3` na inserção, assim como a remoção preemptiva é o que quebra
`m = 3` na remoção. Com os padrões (reativa/reativa) a árvore é correta para
toda ordem, inclusive `m = 3`.

---

## 4. Reaproveitamento de nós — free-list (PPT, slide 2)

**Houve reaproveitamento de nós físicos em disco?** Sim. Slots que ficam livres
quando um nó é descartado são reciclados em vez de abandonados.

**Quando um slot é liberado?** Em dois momentos da remoção:
1. **Fusão de nós** (`mergeChildren`): o nó da direita é absorvido pelo da
   esquerda; o slot da direita deixa de ser referenciado.
2. **Colapso da raiz**: quando a raiz fica com 0 chaves, o único filho assume e
   o slot da raiz antiga deixa de ser referenciado.

**Qual estrutura de dados?** Uma **free-list** implementada como uma **pilha
(LIFO) de índices de slots livres**, em memória durante a execução
(`std::vector<int>`) e **persistida no `.meta`** (após o `rootIdx`, grava-se a
quantidade de slots livres seguida dos índices). Ao reabrir a árvore, a
free-list é recuperada, de modo que o reaproveitamento sobrevive entre sessões.

**Como o reaproveitamento foi implementado?** Toda alocação e liberação de nó
passa por dois helpers da `BTree`, em vez de falar direto com o buffer pool:

- `allocateNode()` — se o reuso está ligado e a free-list não está vazia,
  desempilha um índice e chama `BufferPool::allocateAt(idx)`, que prende um nó
  zerado a esse **slot já existente** (sem crescer o arquivo). Caso contrário,
  chama `BufferPool::allocate()`, que reserva um **slot novo** no fim do arquivo
  (`DiskIO::allocateIdx`).
- `freeNode(idx)` — chama `BufferPool::evict(idx)` (descarta a cópia do nó do
  cache **sem** writeback, pois o conteúdo foi invalidado) e, se o reuso está
  ligado, empilha `idx` na free-list.

O efeito é que o arquivo só cresce quando **não** há slot livre disponível.
Sob cargas com remoções seguidas de novas inserções (*churn*), o arquivo fica
muito mais compacto: nas medições, com reuso ligado o número de slots físicos
fica **igual ao número de nós vivos** (compactação perfeita), enquanto sem reuso
sobram ~20–29 % de slots órfãos.

O mecanismo pode ser **desligado** (`reuseNodes = false`) — exatamente para
medir a ocupação do arquivo *com* e *sem* reaproveitamento, como pede o
enunciado. Com reuso desligado, `freeNode` apenas descarta o nó do cache e o
slot fica órfão (comportamento da versão antiga).

---

## 5. Compilação

Os `.tpp` são **incluídos** pelos respectivos `.h` (no fim de cada header),
então não se compila `.tpp` separadamente: basta compilar cada `.cpp` (que tem
`main`) com os headers no *include path*.

```bash
# driver de benchmark (instancia varias ordens num so binario)
g++ -O2 -std=c++17 benchmark.cpp -o benchmark

# exemplo de uso interativo
g++ -O2 -std=c++17 main.cpp -o main
```

> Por que um único `benchmark.cpp` e não recompilar por ordem? `ORDER` é
> parâmetro de *template* (constante de compilação). Para varrer dezenas de
> ordens, o `benchmark.cpp` instancia todas e despacha em runtime por um
> `switch` (`dispatch()`/`dispatchValidate()`). Para acrescentar uma ordem nova,
> adicione um `case N:` nos dois despachos.
> Ordens já instanciadas: 3, 4, 5, 8, 16, 32, 64, 100, 128, 256, 512, 1000, 1024.

---

## 6. Execução dos benchmarks

O `benchmark` faz duas fases:

1. **Validação** (sempre, salvo `--no-validate`): para cada ordem × família ×
   reuso on/off, confere insert/search/remove contra um `std::set`, inclusive
   após reabrir o arquivo (testa a persistência e a free-list). Escreve um
   relatório em `out/validity.csv` e marca cada ordem como válida por família
   (a preemptiva é marcada N/A em `m = 3`).
2. **Benchmarks**: varre o espaço de parâmetros e emite CSV "tidy" (uma linha
   por fase).

### Suíte curada (padrão, sem flags de varredura)

```bash
mkdir -p out
./benchmark --out out/results.csv
python3 plot_results.py --csv out/results.csv --outdir out/plots
```

Roda quatro experimentos com `N = 100000` (≈ 70 s no total): `order_sweep`,
`size_sweep`, `family_compare` (reativa × preemptiva) e `reuse_compare`
(reuso on × off).

### Varredura manual (para o estudo final, escalando N até 10⁶)

Qualquer flag de varredura desativa a suíte curada e roda **uma** varredura com
o rótulo de `--exp`.

```bash
# EXP A — varredura de ORDEM, N de 10^3 a 10^6, sequencial e aleatorio
./benchmark --exp order_sweep \
  --orders 4,8,16,32,64,128,256,512,1000 \
  --sizes 1000,10000,100000,1000000 \
  --patterns seq,rand --caches 3 --out out/A.csv

# EXP A (m=3) — documenta a ordem pequena (so familia reativa; preemptiva e N/A)
./benchmark --exp order_sweep --orders 3 --sizes 1000,10000,100000 \
  --patterns seq,rand --families reactive --out out/A_m3.csv

# EXP B — comparacao de FAMILIAS (reativa x preemptiva), m>=4
./benchmark --exp family_compare --orders 4,8,16,32,128 --sizes 100000 \
  --patterns seq,rand --families reactive,preemptive --out out/B.csv

# EXP C — REUSO de nos: ocupacao do arquivo com x sem free-list
./benchmark --exp reuse_compare --orders 3,4,8,32,100 --sizes 100000 \
  --patterns rand --families reactive --reuse 1,0 --out out/C.csv

# EXP D — varredura de CACHE (ordem e N fixos)
./benchmark --exp cache_sweep --orders 8,32 --sizes 100000 --patterns rand \
  --caches 2,4,8,16,32,64,128,256 --out out/D.csv
```

Para juntar vários CSVs num só antes de plotar, basta concatenar mantendo um
único cabeçalho:

```bash
head -1 out/A.csv > out/results.csv
for f in A A_m3 B C D; do tail -n +2 out/$f.csv >> out/results.csv; done
python3 plot_results.py --csv out/results.csv --outdir out/plots
```

### Flags

| flag | significado | padrão |
|---|---|---|
| `--orders`   | lista de ordens `m` (vírgula) | `3,4,8,16,32,64,128,256` |
| `--sizes`    | lista de `N` | `1000,10000,100000` |
| `--patterns` | `seq` e/ou `rand` (ordem de inserção) | `seq,rand` |
| `--caches`   | tamanhos do buffer pool | `3` |
| `--families` | `reactive` e/ou `preemptive` | `reactive` |
| `--reuse`    | `1` e/ou `0` (free-list on/off) | `1` |
| `--del-frac` | fração de chaves removidas na fase delete | `0.5` |
| `--search-q` | nº de buscas (limitado a 2·N) | `20000` |
| `--seed`     | semente RNG | `42` |
| `--out`      | caminho do CSV | `out/results.csv` |
| `--exp`      | rótulo do experimento | `order_sweep` |
| `--no-validate` | pula a fase de validação | (valida) |
| `--only-validate` | só valida, sem benchmarks | — |

---

## 7. Gráficos

```bash
pip install pandas matplotlib
python3 plot_results.py --csv out/results.csv --outdir out/plots
```

Gera em `out/plots/`, conforme os experimentos presentes no CSV:
`order_sweep_io_{seq,rand}.png`, `order_sweep_height.png`,
`size_sweep_io_{insert,search,delete}.png`,
`family_compare_{insert,delete}.png`,
`reuse_compare_{slots,savings}.png`, `cache_sweep_io_{insert,search}.png`,
`cpu_vs_io_insert.png`.

---

## 8. Colunas do CSV

Uma linha por `(config, fase)`, com `phase ∈ {insert, search, delete, reinsert}`.
A fase `reinsert` reinsere, após a remoção, a mesma quantidade de chaves novas
(*churn*) — é nela que o efeito da free-list aparece na ocupação.

- `exp` — rótulo do experimento.
- `order, n, pattern, cache` — parâmetros da configuração.
- `family` — `reactive` ou `preemptive`.
- `reuse` — `1`/`0` (free-list ligada?).
- `valid` — 1 se a validação por oráculo passou para essa ordem/família.
- `reads, writes` — acessos **lógicos** ao disco contados pelo `DiskIO` (miss de
  cache / gravação real). Independem do page cache do SO.
- `io_total, io_per_op` — soma e média por operação da fase.
- `wall_s, cpu_user_s, cpu_sys_s, io_wait_s` — `chrono` + `getrusage`;
  `io_wait = wall − (user+sys)`. Como o `DiskIO` faz `flush()` por gravação, o
  custo de I/O aparece sobretudo como **CPU-sistema** (syscalls); a espera de
  dispositivo fica ~0 com page cache quente — por isso a métrica central é o
  contador lógico `reads/writes`.
- `record_bytes` — tamanho de um registro de nó.
- `file_bytes, slots` — tamanho do arquivo e nós alocados (`file_bytes /
  record_bytes`). **Com reuso, `slots` reflete a economia real.**
- `live_nodes` — nós alcançáveis a partir da raiz (BFS no `.dat` cru).
- `height` — número de níveis (medido pós-inserção).

A ocupação **sem reuso** é `slots·record_bytes` com `reuse=0`; **com reuso** é
`slots·record_bytes` com `reuse=1` (que tende a `live_nodes·record_bytes`).

---

## 9. Aderência ao enunciado

| Requisito do enunciado | Onde / como |
|---|---|
| Classe Árvore B parametrizada por `m` | template `BTree<KeyType, ORDER>`. |
| Opera **estritamente** em memória secundária, 1 nó por acesso | `DiskIO` lê/grava um `NodeRecord` por slot; o buffer pool mantém só alguns nós (padrão ≤ 3); a árvore nunca é carregada inteira nem em blocos. |
| Busca, inserção, remoção | `search`, `insert`, `remove` (duas famílias). |
| Impressão hierárquica | `print(maxLevel)` (por níveis, com indentação). |
| **Contador de acessos ao disco** | `DiskIO::reads()/writes()`, reexpostos por `BTree::diskReads()/diskWrites()`. |
| **Reaproveitamento de nós descartados** | free-list persistida (§4); liga/desliga por `reuseNodes`. |
| Variar `m` (pequeno `m=3`, grandes `10²/10³`) e analisar **altura** | `order_sweep` cobre `m=3..1000+`; coluna `height`; gráfico `order_sweep_height.png`. |
| `N = 10³..10⁶`, **aleatório e sequencial** | `--sizes` e `--patterns seq,rand`. |
| **I/O médio por operação** | coluna `io_per_op`. |
| **Ocupação do arquivo com e sem reuso** | `reuse_compare` (`--reuse 1,0`); colunas `file_bytes`/`slots`. |
| **Tempo de CPU vs espera de I/O** | colunas `cpu_user_s`/`cpu_sys_s`/`io_wait_s`; gráfico `cpu_vs_io_insert.png`. |
| Comparação crítica / visualização (tabelas, figuras) | CSV + `plot_results.py`. |
| Não residir em memória principal / não carregar em blocos | garantido pela camada `DiskIO` + buffer pool com cache pequeno. |

---

## 10. Resultados principais (com a configuração padrão)

- **Famílias (inserção).** A reativa faz menos I/O por operação que a
  preemptiva; na inserção **sequencial** a diferença é grande (a preemptiva
  pré-divide de forma ansiosa, enquanto a reativa trabalha sobre a espinha
  direita, que permanece quente no cache). As curvas convergem em `m` grande.
- **Reaproveitamento de nós.** Após *churn*, com reuso o arquivo fica compacto
  (`slots = live_nodes`); sem reuso sobram ~20–29 % de slots órfãos.
- **Ordem × altura.** A altura cai rapidamente com `m` (de ~11 em `m=4` para 2–3
  em `m=100..1000`, com `N=10⁵`), e o I/O por operação acompanha.
- **`m = 3`** é correta e mensurável na família reativa (a preemptiva permanece
  documentada como degenerada).
