#include <iostream>
#include <string>
#include <filesystem>
#include "emu.hpp"

namespace fs = std::filesystem;

// Auto-find the most recently modified .elf in the build dir
static std::string find_elf(const std::string& build_dir) {
    std::string best;
    fs::file_time_type best_time;
    try {
        for (auto& e : fs::directory_iterator(build_dir)) {
            if (e.path().extension() == ".elf") {
                auto t = e.last_write_time();
                if (best.empty() || t > best_time) {
                    best      = e.path().string();
                    best_time = t;
                }
            }
        }
    } catch (...) {}
    return best;
}

int main(int argc, char** argv) {
    std::string elf_path;

    if (argc >= 2) {
        elf_path = argv[1];
    } else {
        // Auto-detect: look in the sibling blink build directory
        const std::string build_dir =
            "../stm32f103_blink/blink/build";
        elf_path = find_elf(build_dir);
        if (elf_path.empty()) {
            std::cerr << "Usage: stm32f103_emu <path/to/firmware.elf>\n"
                      << "  (or place an .elf in " << build_dir << ")\n";
            return 1;
        }
        std::cout << "[EMU] Auto-detected ELF: " << elf_path << "\n";
    }

    std::cout << "STM32F103 Emulator\n";
    std::cout << "ELF: " << elf_path << "\n\n";

    STM32F103Emulator emu;
    if (!emu.loadElf(elf_path)) return 1;

    emu.run(5000000);
    return 0;
}
