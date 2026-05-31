# Implementação de B-Trees em árvores secundária seguindo padrões de SGBD
Projeto da disciplina de mestrado *Algoritmos e estrutura de dados* que objetiva criar uma árvore-B operacionalizada em memória secundária. A estratégia seguida é de cachear nós em memória principal quando necessário.

Feito por: 
- Antonio Pilan
- Thiago Martins
- William Lima

## Estratégia de memória secundária:
Nossa estratégia aborda uma árvore-B que opera carregando os nós da memória secundária em memória principal, executando as operações necessárias e devolvendo elas em memória secundária. $N$ nós só são operacionalizados de maneira indiviudal, partindo do principío que uma árvore gerada por uma base de dados grande não pode ser carregada em memória principal. Portanto, o $n-$ésimo nó é carregado em memória, operacionalizado descarregado em memória secundária de forma que, em memória principal, existem (no máximo) os nós ($n-1$, $n$, $n+1$) que totalizam nó, pai e filho. Portanto, o número $N$ de nós em memória principal seguem a restrição padrão (mutável) de $0\leq N\leq 3$.

Essa estratégia seguida é chamada *Buffer Pool* usando uma estratégia *Lazy Load* dos nós, padrão muito usado em SGBDs e adaptado para nossos critérios.

Sempre que criarmos um arquivo .dat, será salvo um .dat.meta que registra o endereço da raíz para futuras consultas e carregamentos.

### Arquivo disk_manager.h
Interações com o disco são gerenciadas nesse arquivo.

### Arquivo btree.h
Definições das estruturas de dados da árvore.

### Arquivo btree_manager.h
Nesse arquivo estão todas as operações algorítmicas da árvore são implementadas em função pública e relacionadas ao buffer pool e gestão da árvore B em disco em funções privadas voltadas a gerenciamentos. 

### Arquivo bench.cpp
Driver de benchmark compilado por ordem (`make bin/bench_<N>`, com `-DBENCH_ORDER=N`). Recebe `--phase insert|search`, mede tempo (`<chrono>`) e contadores de disco do `BTreeManager`, e emite uma única linha `RESULT,...` parseável. É chamado pelos notebooks via `subprocess`.

## Experimentos

A experimentação está dividida em **dois pipelines independentes**, cada um com seu notebook e pasta de saída:

| pipeline | notebook | pasta de saída | ordem de inserção |
|---|---|---|---|
| sequencial | [`experimentos_ordenado.ipynb`](experimentos_ordenado.ipynb) | `experimentos/exp{1,2,3,custom}/` | ids 1..N crescentes |
| randomizado | [`experimentos_random.ipynb`](experimentos_random.ipynb) | `experimentos_random/` | shuffle determinístico (`RAND_SEED=7`) |

Cada pipeline cobre três variações: **Operações × Ordem**, **Operações × Tamanho do dataset** e **Operações × Cache size**, medindo `reads`, `writes` e `time_s` para insert e search.

## Principais resultados

### Sequencial
- **Penhasco limpo em `cache = altura` da árvore.** Quando o buffer pool comporta a espinha raiz→folha (a única descida quente, pois toda inserção ordenada percorre a borda direita), as leituras vão a **zero** e as escritas caem para uma vez por nó.
- O marcador `ORDER = CACHE` nos gráficos coincide com esse penhasco **apenas** em ordens onde `altura ≈ ordem` (~ordem 8 para N=100k). Para outras ordens, o penhasco se desloca seguindo a altura real, não a ordem.

### Randomizado
- **Sem penhasco.** Como cada inserção desce por um caminho diferente, não existe espinha quente. O cache só consegue economizar nos **níveis altos**, compartilhados por todas as descidas.
- **Writes saturam imediatamente:** ganho máximo de ~1,4 % em todas as ordens, independente do cache. Cada inserção suja uma folha aleatória → folha é despejada antes de ser reusada → escrita inevitável.
- **Reads/search têm ganho real:** até **~65 % de redução** quando o cache começa a comportar a raiz + o nível 1 (e parte do nível 2). A saturação acontece em diferentes caches por ordem — cache ~10 para ordem 16, ~16 para ordem 8, ~32+ para ordem 4 — refletindo o quanto da "coroa" da árvore cabe na memória.

## Como rodar

```bash
python3 -m venv .venv && source .venv/bin/activate
pip install pandas matplotlib nbconvert ipykernel jupyter
# rode o notebook desejado no IDE (Restart & Run All)
```

`make bin/bench_<N>` é chamado sob demanda pelos próprios notebooks — não precisa pré-compilar.

