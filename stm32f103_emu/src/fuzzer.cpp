#include "fuzzer.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;

static std::string hex32(uint32_t v) {
    std::ostringstream s;
    s << "0x" << std::hex << std::setw(8) << std::setfill('0') << v;
    return s.str();
}

// ─── Construction ─────────────────────────────────────────────────────────────

Fuzzer::Fuzzer(const std::string& elf_path, uint64_t max_cycles)
    : max_cycles(max_cycles)
{
    std::cout << "[FUZZ] Loading ELF: " << elf_path << "\n";
    if (!emu.loadElf(elf_path))
        throw std::runtime_error("Failed to load ELF: " + elf_path);

    take_snapshot();

    // Silence all output during fuzzing runs
    emu.periph.verbose = false;
    emu.mem.verbose    = false;

    std::cout << "[FUZZ] Snapshot captured. max_cycles=" << std::dec << max_cycles
              << "  cov_map=" << CPU::COV_MAP_SIZE << " slots\n";
}

// ─── Snapshot ─────────────────────────────────────────────────────────────────

void Fuzzer::take_snapshot() {
    std::copy(std::begin(emu.cpu.R), std::end(emu.cpu.R), snap.R.begin());
    snap.xPSR        = emu.cpu.xPSR;
    snap.PRIMASK     = emu.cpu.PRIMASK;
    snap.BASEPRI     = emu.cpu.BASEPRI;
    snap.CONTROL     = emu.cpu.CONTROL;
    snap.cycle_count = emu.cpu.cycle_count;
    snap.halted      = emu.cpu.halted;
    snap.halt_pc     = emu.cpu.halt_pc;
    snap.it_state    = emu.cpu.it_state;

    snap.sram = emu.mem.sram;

    snap.periph_regs = emu.periph.regs;
    std::copy(std::begin(emu.periph.gpio_odr), std::end(emu.periph.gpio_odr),
              snap.gpio_odr.begin());
    snap.stk_rvr = emu.periph.stk_rvr;
    snap.stk_cvr = emu.periph.stk_cvr;
    snap.stk_csr = emu.periph.stk_csr;
    snap.ticks   = emu.periph.ticks;
}

void Fuzzer::restore_snapshot() {
    std::copy(snap.R.begin(), snap.R.end(), std::begin(emu.cpu.R));
    emu.cpu.xPSR        = snap.xPSR;
    emu.cpu.PRIMASK     = snap.PRIMASK;
    emu.cpu.BASEPRI     = snap.BASEPRI;
    emu.cpu.CONTROL     = snap.CONTROL;
    emu.cpu.cycle_count = snap.cycle_count;
    emu.cpu.halted      = snap.halted;
    emu.cpu.halt_pc     = snap.halt_pc;
    emu.cpu.it_state    = snap.it_state;
    emu.cpu.unimpl_count = 0;
    emu.cpu.cov_prev    = 0;

    emu.mem.sram = snap.sram;

    emu.periph.regs    = snap.periph_regs;
    std::copy(snap.gpio_odr.begin(), snap.gpio_odr.end(),
              std::begin(emu.periph.gpio_odr));
    emu.periph.stk_rvr = snap.stk_rvr;
    emu.periph.stk_cvr = snap.stk_cvr;
    emu.periph.stk_csr = snap.stk_csr;
    emu.periph.ticks   = snap.ticks;
}

// ─── Single run ───────────────────────────────────────────────────────────────

FuzzResult Fuzzer::run_once(const std::vector<uint8_t>& input) {
    restore_snapshot();

    emu.periph.fuzz_data   = input.empty() ? nullptr : input.data();
    emu.periph.fuzz_size   = input.size();
    emu.periph.fuzz_cursor = 0;

    memset(run_map, 0, sizeof(run_map));
    emu.cpu.cov_map  = run_map;
    emu.cpu.cov_prev = 0;

    FuzzResult res;

    uint32_t tight_loop_cnt = 0;
    uint32_t prev_pc        = 0;

    for (uint64_t i = 0; i < max_cycles && !emu.cpu.halted; ++i) {
        uint32_t pc = emu.cpu.R[CPU::PC];

        bool in_flash = pc >= Memory::FLASH_BASE &&
                        pc <  Memory::FLASH_BASE + Memory::FLASH_SIZE;
        bool in_sram  = pc >= Memory::SRAM_BASE  &&
                        pc <  Memory::SRAM_BASE  + Memory::SRAM_SIZE;

        if (!in_flash && !in_sram) {
            res.status   = RunStatus::CRASH;
            res.crash_pc = pc;
            res.reason   = "PC out of range " + hex32(pc) +
                           " LR=" + hex32(emu.cpu.R[CPU::LR]);
            break;
        }

        uint32_t sp = emu.cpu.R[CPU::SP];
        if (sp < Memory::SRAM_BASE || sp > Memory::SRAM_BASE + Memory::SRAM_SIZE) {
            res.status   = RunStatus::CRASH;
            res.crash_pc = pc;
            res.reason   = "SP out of SRAM " + hex32(sp);
            break;
        }

        // Detect branch-to-self (while(1) spin)
        if (pc == prev_pc) {
            if (++tight_loop_cnt > 4) break;
        } else {
            tight_loop_cnt = 0;
        }
        prev_pc = pc;

        emu.cpu.step();

        if (emu.cpu.unimpl_count > 0) break; // unknown instruction — stop cleanly
    }

    if (emu.cpu.halted) {
        res.status   = RunStatus::CRASH;
        res.crash_pc = emu.cpu.halt_pc;
        res.reason   = "CPU halted (BKPT/UDF) at " + hex32(emu.cpu.halt_pc);
    }

    res.cycles = emu.cpu.cycle_count;

    // Merge into global map and flag new edges
    for (size_t i = 0; i < CPU::COV_MAP_SIZE; ++i) {
        if (run_map[i] && !global_map[i]) {
            global_map[i]  = 1;
            res.new_edges  = true;
            ++total_edges;
        }
    }

    emu.cpu.cov_map = nullptr;
    return res;
}

// ─── Mutation ─────────────────────────────────────────────────────────────────

std::vector<uint8_t> Fuzzer::mutate(const std::vector<uint8_t>& seed) {
    static const uint8_t kInteresting[] = {
        0x00, 0x01, 0x02, 0x7F, 0x80, 0xFE, 0xFF, 0x40, 0x10, 0x0A, 0x0D
    };
    static constexpr size_t MAX_LEN = 256;

    auto v = seed;
    if (v.empty()) v.push_back(0);

    int num_muts = 1 + (int)(rng() % 4);
    for (int m = 0; m < num_muts; ++m) {
        if (v.empty()) v.push_back(0);
        size_t idx = rng() % v.size();

        switch (rng() % 8) {
        case 0: // random bit flip
            v[idx] ^= (uint8_t)(1u << (rng() % 8));
            break;
        case 1: // byte invert
            v[idx] ^= 0xFF;
            break;
        case 2: // interesting value
            v[idx] = kInteresting[rng() % (sizeof kInteresting)];
            break;
        case 3: // random byte
            v[idx] = (uint8_t)(rng() & 0xFF);
            break;
        case 4: // insert random byte
            if (v.size() < MAX_LEN)
                v.insert(v.begin() + (ptrdiff_t)idx, (uint8_t)(rng() & 0xFF));
            break;
        case 5: // delete byte
            if (v.size() > 1)
                v.erase(v.begin() + (ptrdiff_t)idx);
            break;
        case 6: { // splice from another corpus entry
            if (corpus.size() > 1) {
                const auto& other = corpus[rng() % corpus.size()];
                size_t rem = v.size() - idx;
                if (!other.empty() && rem > 0) {
                    size_t len = 1 + rng() % std::min(rem, other.size());
                    for (size_t k = 0; k < len; ++k)
                        v[idx + k] = other[k % other.size()];
                }
            }
            break;
        }
        case 7: { // repeat a byte (useful for UART reads)
            uint8_t b   = v[idx];
            size_t  cnt = 2 + rng() % 8;
            for (size_t k = 0; k < cnt && v.size() < MAX_LEN; ++k)
                v.insert(v.begin() + (ptrdiff_t)idx, b);
            break;
        }
        }
    }
    return v;
}

// ─── Crash saving ─────────────────────────────────────────────────────────────

void Fuzzer::save_crash(const std::string& dir,
                        const std::vector<uint8_t>& input,
                        const FuzzResult& r) {
    fs::create_directories(dir);
    std::string base = dir + "/crash_" + std::to_string(total_crashes - 1);

    {
        std::ofstream f(base + ".bin", std::ios::binary);
        f.write(reinterpret_cast<const char*>(input.data()),
                (std::streamsize)input.size());
    }
    {
        std::ofstream f(base + ".txt");
        f << "Reason:     " << r.reason      << "\n"
          << "PC:         " << hex32(r.crash_pc) << "\n"
          << "Cycles:     " << r.cycles      << "\n"
          << "Input size: " << input.size()  << " bytes\n"
          << "Input hex: ";
        for (uint8_t b : input)
            f << " " << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        f << "\n";
    }
}

// ─── Stats ────────────────────────────────────────────────────────────────────

void Fuzzer::print_stats(uint64_t iter) const {
    std::cout << "[FUZZ] iter=" << std::setw(8) << std::dec << iter
              << "  corpus="   << std::setw(4) << corpus.size()
              << "  edges="    << std::setw(5) << total_edges
              << "  crashes="  << total_crashes
              << "  runs="     << total_runs    << "\n";
}

// ─── Replay (verbose single run) ─────────────────────────────────────────────

void Fuzzer::replay(const std::string& crash_bin) {
    std::ifstream f(crash_bin, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open: " + crash_bin);
    std::vector<uint8_t> input(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>());

    std::cout << "[FUZZ] Replaying " << crash_bin
              << "  (" << input.size() << " bytes)\n";

    // Restore snapshot with verbose on
    restore_snapshot();
    emu.periph.verbose = true;
    emu.mem.verbose    = true;

    emu.periph.fuzz_data   = input.empty() ? nullptr : input.data();
    emu.periph.fuzz_size   = input.size();
    emu.periph.fuzz_cursor = 0;

    emu.run(max_cycles);
}

// ─── Main fuzzing loop ────────────────────────────────────────────────────────

void Fuzzer::run(uint64_t iterations, const std::string& crash_dir) {
    fs::create_directories(crash_dir);

    // Seed corpus (no empty vectors — they can trigger div-by-zero in crossover)
    corpus.push_back({0x00});
    corpus.push_back({0x00, 0x00, 0x00, 0x00});
    corpus.push_back({0xFF, 0xFF, 0xFF, 0xFF});
    corpus.push_back({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
    corpus.push_back({0x41, 0x42, 0x43, 0x44}); // "ABCD"
    corpus.push_back({0x0D, 0x0A});              // CR LF (UART typical)
    corpus.push_back({0x7F, 0x80, 0x7F, 0x80}); // boundary values

    // Warm-up: populate global_map from seed corpus
    std::cout << "[FUZZ] Warm-up with " << corpus.size() << " seeds...\n";
    for (const auto& seed : corpus) {
        run_once(seed);
        ++total_runs;
    }
    std::cout << std::dec << "[FUZZ] Initial edges: " << total_edges
              << "  Starting " << iterations << " iterations...\n\n";

    auto t_start = std::chrono::steady_clock::now();

    for (uint64_t iter = 0; iter < iterations; ++iter) {
        size_t  seed_idx = iter % corpus.size();
        auto    input    = mutate(corpus[seed_idx]);
        auto    res      = run_once(input);
        ++total_runs;

        if (res.new_edges)
            corpus.push_back(input);

        if (res.status == RunStatus::CRASH) {
            ++total_crashes;
            save_crash(crash_dir, input, res);
            std::cout << "[FUZZ] CRASH #" << total_crashes
                      << " pc=" << hex32(res.crash_pc)
                      << "  " << res.reason << "\n"
                      << "       saved -> " << crash_dir
                      << "/crash_" << (total_crashes - 1) << "\n";
        }

        if (iter % 50000 == 0) {
            auto   t_now   = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(t_now - t_start).count();
            double rate    = elapsed > 0.0 ? (double)total_runs / elapsed : 0.0;
            std::cout << "[FUZZ] " << (uint64_t)rate << " runs/s  ";
            print_stats(iter);
        }
    }

    auto   t_end    = std::chrono::steady_clock::now();
    double elapsed  = std::chrono::duration<double>(t_end - t_start).count();
    double rate     = elapsed > 0.0 ? (double)total_runs / elapsed : 0.0;

    std::cout << "\n[FUZZ] Done in " << (int)elapsed << "s"
              << "  " << (uint64_t)rate << " runs/s\n";
    print_stats(iterations);
    std::cout << "[FUZZ] Crashes saved in: " << crash_dir << "/\n";
}
