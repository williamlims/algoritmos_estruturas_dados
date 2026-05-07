#ifndef DISK_MANAGER_H
#define DISK_MANAGER_H

#include <string>
#include "btree.h"

class DiskIO {
public:
    static void createFile(const std::string& filename);
    static bool writeNode(const std::string& filename, int index, const Node& node);
    static bool readNode(const std::string& filename, int index, Node& node);
};

#endif