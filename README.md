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

## Experimentos:

### Operações x Ordem 
Para um mesmo dataset, entender como o tempo das operações se comportam com o aumento da ordem da árvore.

### Operações x Tamanho do dataset
Para uma ordem fixa, identificar quanto o tempo das operações escala com o aumento do dataset.

### Operações x Cache size 
Analisar como o tempo de execução de operações se comporta com o aumento dos nós em cache.

