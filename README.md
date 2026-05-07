# Implementação de B-Trees em árvores secundária seguindo padrões de SGBD

## Estratégia de memória secundária:
Uma árvore opera carregando os nós em memória principal, executando as operações necessárias e carregando elas em memória secundária. $N$ nós só são operacionalizados de maneira indiviudal, partindo do principío que uma árvore gerada por uma base de dados grande não pode ser carregada em memória principal.

Essa estratégia seguida é chamada *Buffer Pool* usando uma estratégia *Lazy Load* dos nós, padrão muito usado em SGBDs.

Sempre que criarmos um arquivo .dat, será salvo um .dat.meta que registra o endereço da raíz para futuras consultas e carregamentos.

### Arquivo disk_manager.h
Interações com o disco são gerenciadas nesse arquivo.

### Arquivo btree_manager.h
Nesse arquivo estão todas as operações relacionadas ao buffer pool e gestão da árvore B em disco. 

### Arquivo btree.h
Definições da árvore de maneira mais legível, implementado em memória primária fazendo carregamentos e deleções via funções do btree_manager.h

A árvore está carregada em memória primária para ser operacionalizada.