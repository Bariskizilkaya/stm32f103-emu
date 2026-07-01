#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <random>
#include <array>
#include <map>
#include "emu.hpp"

enum class RunStatus { OK, CRASH };

struct FuzzResult {
    RunStatus   status    = RunStatus::OK;
    uint32_t    crash_pc  = 0;
    std::string reason;
    uint64_t    cycles    = 0;
    bool        new_edges = false;
};

// Complete snapshot of emulator state (taken once after ELF load).
struct Snapshot {
    std::array<uint32_t, 16> R;
    uint32_t xPSR, PRIMASK, BASEPRI, CONTROL;
    uint64_t cycle_count;
    bool     halted;
    uint32_t halt_pc;
    uint8_t  it_state;

    std::array<uint8_t, Memory::SRAM_SIZE> sram;

    std::map<uint32_t, uint32_t>  periph_regs;
    std::array<uint32_t, 4>       gpio_odr;
    uint32_t stk_rvr, stk_cvr, stk_csr;
    uint64_t ticks;
};

class Fuzzer {
public:
    static constexpr uint64_t DEFAULT_MAX_CYCLES = 100000;

    Fuzzer(const std::string& elf_path, uint64_t max_cycles = DEFAULT_MAX_CYCLES);

    // Run for `iterations` fuzz cases, saving crashes under `crash_dir/`.
    void run(uint64_t iterations = 1000000, const std::string& crash_dir = "crashes");

    // Replay a saved crash input through the emulator with full verbose output.
    void replay(const std::string& crash_bin);

private:
    STM32F103Emulator emu;
    Snapshot          snap;
    uint64_t          max_cycles;

    std::mt19937 rng{std::random_device{}()};

    std::vector<std::vector<uint8_t>> corpus;

    // AFL-style edge map: slot = (prev_pc>>1 ^ pc>>1) % COV_MAP_SIZE
    uint8_t global_map[CPU::COV_MAP_SIZE]{};
    uint8_t run_map   [CPU::COV_MAP_SIZE];

    uint64_t total_runs    = 0;
    uint64_t total_crashes = 0;
    uint64_t total_edges   = 0;

    void       take_snapshot();
    void       restore_snapshot();
    FuzzResult run_once(const std::vector<uint8_t>& input);
    std::vector<uint8_t> mutate(const std::vector<uint8_t>& seed);
    void       save_crash(const std::string& dir,
                          const std::vector<uint8_t>& input,
                          const FuzzResult& r);
    void       print_stats(uint64_t iter) const;
};
