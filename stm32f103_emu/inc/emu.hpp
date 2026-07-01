#pragma once
#include <string>
#include "memory.hpp"
#include "peripherals.hpp"
#include "cpu.hpp"

class STM32F103Emulator {
public:
    Memory      mem;
    Peripherals periph;
    CPU         cpu;

    STM32F103Emulator() : cpu(mem) { mem.periph = &periph; }

    bool loadElf(const std::string& path);
    void run(uint64_t max_cycles = 2000000);
};
