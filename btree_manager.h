#ifndef BTREE_MANAGER_H
#define BTREE_MANAGER_H

#include "disk_manager.h"
#include "btree.h"
#include <iostream>

class BTreeManager {
public:
    void getChild(node,i);
    void allocateNode();
    void markDirty(node);
    void flush();

};

#endif
