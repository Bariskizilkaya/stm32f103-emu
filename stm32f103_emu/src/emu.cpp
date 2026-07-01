#include "emu.hpp"
#include "elfloader.hpp"
#include <iostream>
#include <iomanip>

bool STM32F103Emulator::loadElf(const std::string& path) {
    ElfLoader elf;
    if (!elf.load(path)) {
        std::cerr << "[EMU] Failed to load ELF: " << path << "\n";
        return false;
    }

    for (auto& seg : elf.segments) {
        uint32_t addr = seg.vaddr;
        uint32_t sz   = seg.data.size();
        // Load into flash or SRAM
        for (uint32_t i = 0; i < sz; ++i)
            mem.write8(addr + i, seg.data[i]);
        std::cout << "[EMU] Loaded segment at 0x" << std::hex << addr
                  << "  size=" << std::dec << sz << " bytes\n";
    }

    // Vector table at flash base: word0=initial SP, word1=reset vector
    uint32_t sp_init  = mem.read32(Memory::FLASH_BASE + 0);
    uint32_t reset_pc = mem.read32(Memory::FLASH_BASE + 4);
    std::cout << "[EMU] Initial SP=0x" << std::hex << sp_init
              << "  Reset PC=0x" << reset_pc << "\n";

    cpu.reset(sp_init, reset_pc);
    return true;
}

void STM32F103Emulator::run(uint64_t max_cycles) {
    std::cout << "[EMU] Starting execution...\n";

    uint32_t prev_pc = 0;
    uint64_t tight_loop = 0;

    for (uint64_t i = 0; i < max_cycles && !cpu.halted; ++i) {
        uint32_t pc = cpu.R[CPU::PC];

        // Detect out-of-flash PC (likely a decode bug)
        if ((pc < Memory::FLASH_BASE || pc >= Memory::FLASH_BASE + Memory::FLASH_SIZE) &&
            (pc < Memory::SRAM_BASE  || pc >= Memory::SRAM_BASE  + Memory::SRAM_SIZE)) {
            std::cout << "[EMU] PC=0x" << std::hex << pc
                      << " out of addressable range at cycle " << std::dec << i
                      << "  LR=0x" << std::hex << cpu.R[14] << "\n";
            break;
        }

        // Detect tight loop (branch-to-self)
        if (pc == prev_pc) {
            ++tight_loop;
            if (tight_loop > 4) {
                std::cout << "[EMU] Infinite loop detected at PC=0x"
                          << std::hex << pc << "\n";
                break;
            }
        } else {
            tight_loop = 0;
        }
        prev_pc = pc;

        cpu.step();
    }

    std::cout << "[EMU] Executed " << std::dec << cpu.cycle_count << " cycles\n";
    cpu.dump();

    // Print GPIO state
    std::cout << "\n[GPIO State]\n";
    const char* names[] = { "GPIOA", "GPIOB", "GPIOC", "GPIOD" };
    static const uint32_t bases[] = { GPIOA_BASE, GPIOB_BASE, GPIOC_BASE, GPIOD_BASE };
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        // Show if CRL (off 0) or CRH (off 4) or BSRR (off 0x10) was written
        bool configured = periph.regs.count(bases[i]) ||
                          periph.regs.count(bases[i] + 4) ||
                          periph.regs.count(bases[i] + 0x10);
        if (!configured) continue;
        any = true;
        std::cout << "  " << names[i] << "_ODR = 0x"
                  << std::hex << std::setw(4) << std::setfill('0') << periph.gpio_odr[i];
        for (int j = 0; j < 16; ++j) {
            if ((periph.gpio_odr[i] >> j) & 1)
                std::cout << "  P" << names[i][4] << std::dec << j << "=1";
        }
        std::cout << "\n";
    }
    if (!any) std::cout << "  (no GPIO ports configured)\n";
}
