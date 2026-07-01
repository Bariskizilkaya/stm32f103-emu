#pragma once
#include <cstdint>
#include <cstddef>
#include "memory.hpp"

class CPU {
public:
    static constexpr int    SP = 13;
    static constexpr int    LR = 14;
    static constexpr int    PC = 15;

    // AFL-style edge coverage: slot = (prev_pc>>1 ^ pc>>1) % COV_MAP_SIZE
    static constexpr size_t COV_MAP_SIZE = 65536;

    uint32_t R[16] = {};
    uint32_t xPSR    = 0x01000000u; // Thumb bit in EPSR
    uint32_t PRIMASK = 0;
    uint32_t BASEPRI = 0;
    uint32_t CONTROL = 0;

    uint64_t cycle_count  = 0;
    bool     halted       = false;
    uint32_t halt_pc      = 0;
    uint32_t unimpl_count = 0; // incremented on each unimplemented instruction
    uint8_t  it_state     = 0; // ITSTATE (exposed for snapshot/restore)

    // Coverage hooks (null = disabled)
    uint8_t* cov_map  = nullptr; // COV_MAP_SIZE byte array owned by caller
    uint32_t cov_prev = 0;       // previous PC for edge hash

    Memory& mem;
    explicit CPU(Memory& mem) : mem(mem) {}

    void reset(uint32_t sp, uint32_t pc);
    void step();
    void dump() const;

    bool N() const { return (xPSR >> 31) & 1; }
    bool Z() const { return (xPSR >> 30) & 1; }
    bool C() const { return (xPSR >> 29) & 1; }
    bool V() const { return (xPSR >> 28) & 1; }
    void setN(bool v) { xPSR = v ? xPSR|(1u<<31) : xPSR&~(1u<<31); }
    void setZ(bool v) { xPSR = v ? xPSR|(1u<<30) : xPSR&~(1u<<30); }
    void setC(bool v) { xPSR = v ? xPSR|(1u<<29) : xPSR&~(1u<<29); }
    void setV(bool v) { xPSR = v ? xPSR|(1u<<28) : xPSR&~(1u<<28); }

private:
    bool pc_written = false;

    void exec16(uint16_t hw, uint32_t ia);
    void exec32(uint32_t instr, uint32_t ia);

    bool cond_check(uint8_t cond);
    void update_nz(uint32_t r);
    void add_with_carry(uint32_t a, uint32_t b, bool cin,
                        uint32_t& result, bool& cout, bool& vout);

    uint32_t thumb_expand_imm(uint16_t imm12, bool* carry = nullptr);
    // shift types: 0=LSL, 1=LSR, 2=ASR, 3=ROR/RRX
    uint32_t shift(uint32_t val, uint8_t type, uint8_t amount, bool cin, bool* cout = nullptr);

    // helpers
    uint32_t pc_read() const { return R[PC]; } // during exec, R[PC] = ia+4
    void branch_to(uint32_t addr);
    void blx_to(uint32_t addr);
    void push_regs(uint16_t reglist);
    void pop_regs(uint16_t reglist);
    void stm(uint32_t base_reg, uint16_t reglist, bool writeback);
    void ldm(uint32_t base_reg, uint16_t reglist, bool writeback);
};
