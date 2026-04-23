#ifndef BTREE_MANAGER_H
#define BTREE_MANAGER_H

#include "btree.h"
#include <iostream>

class BTreeManager {
private:
    BTree<int, 3> tree;  // B-Tree with int keys, order 3

public:
    void run() {
        std::cout << "B-Tree Manager initialized\n";

    }
};

#endif
