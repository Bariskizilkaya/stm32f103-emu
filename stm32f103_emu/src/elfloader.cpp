#include "elfloader.hpp"
#include <fstream>
#include <cstring>

// ELF32 structures
struct Elf32_Ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
};

static const uint32_t PT_LOAD = 1;

bool ElfLoader::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    Elf32_Ehdr ehdr;
    f.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (!f || memcmp(ehdr.e_ident, "\x7f""ELF", 4) != 0) return false;
    if (ehdr.e_ident[4] != 1) return false; // not ELF32

    entry = ehdr.e_entry;
    segments.clear();

    for (int i = 0; i < ehdr.e_phnum; ++i) {
        f.seekg(ehdr.e_phoff + i * ehdr.e_phentsize);
        Elf32_Phdr phdr;
        f.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
        if (!f || phdr.p_type != PT_LOAD || phdr.p_memsz == 0) continue;

        ElfSegment seg;
        seg.vaddr = phdr.p_vaddr;
        seg.memsz = phdr.p_memsz;
        seg.data.resize(phdr.p_memsz, 0);

        if (phdr.p_filesz > 0) {
            f.seekg(phdr.p_offset);
            f.read(reinterpret_cast<char*>(seg.data.data()), phdr.p_filesz);
        }
        segments.push_back(std::move(seg));
    }
    return !segments.empty();
}
