#pragma once
#include <cstdint>
#include <array>

class Peripherals;

class Memory {
public:
    static constexpr uint32_t FLASH_BASE = 0x08000000u;
    static constexpr uint32_t FLASH_SIZE = 128u * 1024u;
    static constexpr uint32_t SRAM_BASE  = 0x20000000u;
    static constexpr uint32_t SRAM_SIZE  = 20u * 1024u;

    std::array<uint8_t, FLASH_SIZE> flash{};
    std::array<uint8_t, SRAM_SIZE>  sram{};
    Peripherals* periph  = nullptr;
    bool         verbose = true;

    uint32_t read32(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint8_t  read8 (uint32_t addr);
    void write32(uint32_t addr, uint32_t val);
    void write16(uint32_t addr, uint16_t val);
    void write8 (uint32_t addr, uint8_t  val);

private:
    uint8_t* ptr(uint32_t addr);
};
