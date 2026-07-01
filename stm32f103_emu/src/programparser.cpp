#include <iostream>
#include <map>
#include <iomanip>
#include <fstream>
#include "programparser.hpp"

uint8_t ProgramParser::hexByte(const std::string& s, size_t i) {
        return static_cast<uint8_t>(
        std::stoi(s.substr(i, 2), nullptr, 16)
    );
}
void ProgramParser::parse(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return;
    }

    std::string line;
    std::map<uint32_t, uint8_t> memory;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] != ':')
            continue;

        size_t pos = 1;

        uint8_t byteCount = this->hexByte(line, pos);
        pos += 2;

        uint16_t address =
            (this->hexByte(line, pos) << 8) |
             this->hexByte(line, pos + 2);
        pos += 4;

        uint8_t recordType = this->hexByte(line, pos);
        pos += 2;

        if (recordType != 0x00)
            continue;

        for (int i = 0; i < byteCount; i++) {
            uint8_t data = this->hexByte(line, pos);
            pos += 2;

            memory[address + i] = data;
        }
    }

    for (const auto& kv : memory) {
        std::cout << std::hex << kv.first << ": "
                  << std::setw(2) << std::setfill('0')
                  << (int)kv.second << "\n";
    }
}
