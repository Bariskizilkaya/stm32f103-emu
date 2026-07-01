#include <vector>
#include <cstdint>

class ProgramParser {
public:
    std::vector<std::string> instructions;
    void parse(const std::string& filePath);
    uint8_t hexByte(const std::string& s, size_t i);
};
