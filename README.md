# Implementação de B-Trees em árvores secundária seguindo padrões de SGBD

# Estratégia de memória secundária:
Uma árvore opera carregando os nós em memória principal, executando as operações necessárias e carregando elas em memória secundária. $N$ nós só são operacionalizados de maneira indiviudal, partindo do principío que uma árvore gerada por uma base de dados grande não pode ser carregada em memória principal.

Essa estratégia seguida é chamada *Buffer Pool*, padrão clássico de SGBDs.