#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ElfSegment {
    uint32_t vaddr;
    std::vector<uint8_t> data;
    uint32_t memsz;
};

class ElfLoader {
public:
    uint32_t entry = 0;
    std::vector<ElfSegment> segments;
    bool load(const std::string& path);
};
