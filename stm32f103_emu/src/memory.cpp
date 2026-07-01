#include "memory.hpp"
#include "peripherals.hpp"
#include <cstring>
#include <iostream>
#include <iomanip>

uint8_t* Memory::ptr(uint32_t addr) {
    if (addr >= FLASH_BASE && addr < FLASH_BASE + FLASH_SIZE)
        return flash.data() + (addr - FLASH_BASE);
    if (addr >= SRAM_BASE && addr < SRAM_BASE + SRAM_SIZE)
        return sram.data() + (addr - SRAM_BASE);
    // Boot alias: 0x00000000 maps to flash
    if (addr < FLASH_SIZE)
        return flash.data() + addr;
    return nullptr;
}

uint32_t Memory::read32(uint32_t addr) {
    uint8_t* p = ptr(addr);
    if (p) {
        uint32_t v;
        memcpy(&v, p, 4);
        return v;
    }
    if (periph) return periph->read(addr);
    if (verbose)
        std::cout << "[MEM] unmapped read32 @ 0x" << std::hex << addr << std::endl;
    return 0;
}

uint16_t Memory::read16(uint32_t addr) {
    uint8_t* p = ptr(addr);
    if (p) {
        uint16_t v;
        memcpy(&v, p, 2);
        return v;
    }
    return (uint16_t)read32(addr);
}

uint8_t Memory::read8(uint32_t addr) {
    uint8_t* p = ptr(addr);
    if (p) return *p;
    return (uint8_t)read32(addr);
}

void Memory::write32(uint32_t addr, uint32_t val) {
    uint8_t* p = ptr(addr);
    if (p) { memcpy(p, &val, 4); return; }
    if (periph) { periph->write(addr, val, 4); return; }
    if (verbose)
        std::cout << "[MEM] unmapped write32 @ 0x" << std::hex << addr << " = 0x" << val << std::endl;
}

void Memory::write16(uint32_t addr, uint16_t val) {
    uint8_t* p = ptr(addr);
    if (p) { memcpy(p, &val, 2); return; }
    if (periph) { periph->write(addr, val, 2); return; }
}

void Memory::write8(uint32_t addr, uint8_t val) {
    uint8_t* p = ptr(addr);
    if (p) { *p = val; return; }
    if (periph) { periph->write(addr, val, 1); return; }
}
