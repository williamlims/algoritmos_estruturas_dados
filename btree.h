#ifndef BTREE_H
#define BTREE_H

#include <string>

const int N = 3;

struct Node {
    int n;
    int A[N];      // Filhos
    int K[N - 1];  // Keys
};

struct SearchResult {
    int nodeIndex;
    int keyIndex;
    bool found;
};

SearchResult mSearch(const std::string& filename, int rootIndex, int x);

#endif // BTREE_H