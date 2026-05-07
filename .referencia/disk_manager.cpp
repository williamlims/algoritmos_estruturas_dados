#include "disk_manager.h"
#include <fstream>

void DiskIO::createFile(const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
}

bool DiskIO::writeNode(const std::string& filename, int index, const Node& node) {
    std::fstream file(filename, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) return false;
    file.seekp((index - 1) * sizeof(Node));
    file.write(reinterpret_cast<const char*>(&node), sizeof(Node));
    return file.good();
}

bool DiskIO::readNode(const std::string& filename, int index, Node& node) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return false;
    file.seekg((index - 1) * sizeof(Node));
    file.read(reinterpret_cast<char*>(&node), sizeof(Node));
    return file.good();
}