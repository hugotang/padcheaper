#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace padcheaper {
struct Mapping {
    uint64_t start = 0, end = 0, offset = 0;
    bool readable = false, writable = false, executable = false;
    std::string path;
};
bool ParseMapping(const std::string &line, Mapping &output);
bool IsGameLibrary(const std::string &path);
bool ReadableRange(const std::vector<Mapping> &mappings, uint64_t address, uint64_t size);
uint16_t ElfMachine(const unsigned char *bytes, size_t size);
struct LibraryInventory {
    std::string status = "not-loaded";
    uint64_t mappedBytes = 0;
    uint16_t machine = 0;
    bool executableMapping = false;
};
LibraryInventory InspectGameLibrary();
}
