#include <iostream>
#include <string>
#include "fuzzer.hpp"

static void usage(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " <firmware.elf> [iterations] [max_cycles] [crash_dir]\n"
        << "  " << prog << " <firmware.elf> --replay <crash.bin>\n"
        << "\nDefaults: iterations=1000000  max_cycles=100000  crash_dir=crashes\n";
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::string elf = argv[1];

    // Replay mode
    if (argc >= 4 && std::string(argv[2]) == "--replay") {
        try {
            Fuzzer fuzz(elf);
            fuzz.replay(argv[3]);
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    uint64_t    iterations = argc >= 3 ? std::stoull(argv[2]) : 1000000ULL;
    uint64_t    max_cycles = argc >= 4 ? std::stoull(argv[3]) : Fuzzer::DEFAULT_MAX_CYCLES;
    std::string crash_dir  = argc >= 5 ? argv[4] : "crashes";

    try {
        Fuzzer fuzz(elf, max_cycles);
        fuzz.run(iterations, crash_dir);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
