#include "cpu.hpp"
#include <iostream>
#include <iomanip>
#include <cassert>

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

static uint32_t sign_extend(uint32_t val, int bits) {
    uint32_t mask = 1u << (bits - 1);
    return (val ^ mask) - mask;
}

static uint32_t ror32(uint32_t val, int n) {
    n &= 31;
    if (n == 0) return val;
    return (val >> n) | (val << (32 - n));
}

void CPU::update_nz(uint32_t r) {
    setN(r >> 31);
    setZ(r == 0);
}

void CPU::add_with_carry(uint32_t a, uint32_t b, bool cin,
                          uint32_t& result, bool& cout, bool& vout) {
    uint64_t u = (uint64_t)a + b + cin;
    result = (uint32_t)u;
    cout   = (u >> 32) & 1;
    int64_t s = (int64_t)(int32_t)a + (int32_t)b + cin;
    vout   = (s > INT32_MAX || s < INT32_MIN);
}

// ARM ThumbExpandImm for Thumb-2 modified immediate
uint32_t CPU::thumb_expand_imm(uint16_t imm12, bool* carry_out) {
    uint8_t rot  = (imm12 >> 8) & 0xF;
    uint8_t imm8 = imm12 & 0xFF;
    if (rot == 0) {
        if (carry_out) *carry_out = C();
        return imm8;
    }
    if (rot == 1) {
        if (carry_out) *carry_out = C();
        return ((uint32_t)imm8 << 16) | imm8;
    }
    if (rot == 2) {
        if (carry_out) *carry_out = C();
        return ((uint32_t)imm8 << 24) | ((uint32_t)imm8 << 8);
    }
    if (rot == 3) {
        if (carry_out) *carry_out = C();
        return ((uint32_t)imm8 << 24) | ((uint32_t)imm8 << 16) |
               ((uint32_t)imm8 << 8) | imm8;
    }
    // rotate: value = 1:imm12[6:0], rotated by imm12[11:7] (5-bit)
    uint8_t rot5 = (imm12 >> 7) & 0x1F;
    uint32_t v = 0x80u | (imm12 & 0x7F);
    uint32_t r = ror32(v, rot5);
    if (carry_out) *carry_out = (r >> 31) & 1;
    return r;
}

// Barrel shifter: type 0=LSL,1=LSR,2=ASR,3=ROR/RRX
uint32_t CPU::shift(uint32_t val, uint8_t type, uint8_t amount, bool cin, bool* cout) {
    if (amount == 0) {
        if (cout) {
            if (type == 3) *cout = cin; // RRX carry
            else *cout = C();
        }
        if (type == 3) { // RRX when amount==0 (encoded as ROR #0 → RRX)
            uint32_t r = (val >> 1) | ((uint32_t)cin << 31);
            if (cout) *cout = val & 1;
            return r;
        }
        return val;
    }
    uint32_t r = 0;
    bool c = false;
    switch (type) {
    case 0: // LSL
        if (amount >= 32) { c = (amount == 32) ? (val & 1) : 0; r = 0; }
        else { c = (val >> (32 - amount)) & 1; r = val << amount; }
        break;
    case 1: // LSR
        if (amount >= 32) { c = (amount == 32) ? ((val >> 31) & 1) : 0; r = 0; }
        else { c = (val >> (amount - 1)) & 1; r = val >> amount; }
        break;
    case 2: // ASR
        if (amount >= 32) { c = (val >> 31) & 1; r = (int32_t)val >> 31; }
        else { c = (val >> (amount - 1)) & 1; r = (uint32_t)((int32_t)val >> amount); }
        break;
    case 3: // ROR
        amount &= 31;
        if (amount == 0) { c = (val >> 31) & 1; r = val; }
        else { r = ror32(val, amount); c = (r >> 31) & 1; }
        break;
    }
    if (cout) *cout = c;
    return r;
}

// ──────────────────────────────────────────────────────────────────────────────
// Branch helpers
// ──────────────────────────────────────────────────────────────────────────────

bool CPU::cond_check(uint8_t cond) {
    switch (cond & 0xF) {
    case 0x0: return Z();           // EQ
    case 0x1: return !Z();          // NE
    case 0x2: return C();           // CS/HS
    case 0x3: return !C();          // CC/LO
    case 0x4: return N();           // MI
    case 0x5: return !N();          // PL
    case 0x6: return V();           // VS
    case 0x7: return !V();          // VC
    case 0x8: return C() && !Z();   // HI
    case 0x9: return !C() || Z();   // LS
    case 0xA: return N() == V();    // GE
    case 0xB: return N() != V();    // LT
    case 0xC: return !Z() && (N()==V()); // GT
    case 0xD: return Z() || (N()!=V()); // LE
    case 0xE: return true;           // AL
    case 0xF: return true;           // (AL)
    }
    return false;
}

void CPU::branch_to(uint32_t addr) {
    R[PC] = addr & ~1u;
    pc_written = true;
}

void CPU::blx_to(uint32_t addr) {
    R[PC] = addr & ~1u;
    pc_written = true;
}

void CPU::push_regs(uint16_t reglist) {
    // Push highest number first (stores at lowest address)
    for (int i = 15; i >= 0; --i) {
        if (reglist & (1 << i)) {
            R[SP] -= 4;
            mem.write32(R[SP], R[i]);
        }
    }
}

void CPU::pop_regs(uint16_t reglist) {
    for (int i = 0; i <= 15; ++i) {
        if (reglist & (1 << i)) {
            R[i] = mem.read32(R[SP]);
            R[SP] += 4;
            if (i == PC) { R[PC] &= ~1u; pc_written = true; }
        }
    }
}

void CPU::stm(uint32_t base_reg, uint16_t reglist, bool writeback) {
    uint32_t addr = R[base_reg];
    for (int i = 0; i <= 15; ++i) {
        if (reglist & (1 << i)) {
            mem.write32(addr, R[i]);
            addr += 4;
        }
    }
    if (writeback) R[base_reg] = addr;
}

void CPU::ldm(uint32_t base_reg, uint16_t reglist, bool writeback) {
    uint32_t addr = R[base_reg];
    for (int i = 0; i <= 15; ++i) {
        if (reglist & (1 << i)) {
            uint32_t v = mem.read32(addr);
            addr += 4;
            if (i == PC) { R[PC] = v & ~1u; pc_written = true; }
            else R[i] = v;
        }
    }
    if (writeback && !(reglist & (1 << base_reg))) R[base_reg] = addr;
}

// ──────────────────────────────────────────────────────────────────────────────
// Reset / step
// ──────────────────────────────────────────────────────────────────────────────

void CPU::reset(uint32_t sp, uint32_t pc) {
    for (auto& r : R) r = 0;
    R[SP] = sp;
    R[PC] = pc & ~1u;
    xPSR = 0x01000000u;
    PRIMASK = 0; BASEPRI = 0; CONTROL = 0;
    halted = false; cycle_count = 0; it_state = 0; unimpl_count = 0; cov_prev = 0;
}

void CPU::step() {
    uint32_t ia = R[PC]; // instruction address
    pc_written = false;

    if (cov_map) {
        uint32_t slot = ((cov_prev >> 1) ^ (ia >> 1)) & (COV_MAP_SIZE - 1);
        cov_map[slot]++;
        cov_prev = ia;
    }

    // PC reads as ia+4 for PC-relative calc (Thumb convention)
    R[PC] = ia + 4;

    uint16_t hw1 = mem.read16(ia);
    bool is32 = ((hw1 >> 11) == 0b11101) ||
                ((hw1 >> 11) == 0b11110) ||
                ((hw1 >> 11) == 0b11111);

    if (is32) {
        uint16_t hw2 = mem.read16(ia + 2);
        uint32_t instr = ((uint32_t)hw1 << 16) | hw2;
        exec32(instr, ia);
        if (!pc_written) R[PC] = ia + 4;
    } else {
        exec16(hw1, ia);
        if (!pc_written) R[PC] = ia + 2;
    }

    ++cycle_count;
}

// ──────────────────────────────────────────────────────────────────────────────
// 16-bit Thumb instruction executor
// ──────────────────────────────────────────────────────────────────────────────

void CPU::exec16(uint16_t hw, uint32_t ia) {
    uint8_t op15_10 = hw >> 10; // bits [15:10]

    // ── Shift/Add/Sub/Move/Compare (bits[15:10] = 00xxxx) ──────────────────
    if (op15_10 < 0x10) {
        uint8_t op = (hw >> 9) & 0x1F; // bits[13:9]
        if (op < 8) {
            // LSL immediate (00 000 xx): op[4:3]=00, op[2:0]=shift amount bit
            // Actually: bits[15:11] select:
            uint8_t op15_11 = hw >> 11;
            if (op15_11 == 0b00000) { // LSLS imm
                uint8_t imm5 = (hw >> 6) & 0x1F;
                uint8_t Rm   = (hw >> 3) & 0x7;
                uint8_t Rd   = hw & 0x7;
                bool cy;
                R[Rd] = shift(R[Rm], 0, imm5, C(), &cy);
                if (imm5) setC(cy);
                update_nz(R[Rd]);
            } else if (op15_11 == 0b00001) { // LSRS imm
                uint8_t imm5 = (hw >> 6) & 0x1F;
                uint8_t Rm   = (hw >> 3) & 0x7;
                uint8_t Rd   = hw & 0x7;
                uint8_t amt  = imm5 ? imm5 : 32;
                bool cy;
                R[Rd] = shift(R[Rm], 1, amt, C(), &cy);
                setC(cy); update_nz(R[Rd]);
            } else if (op15_11 == 0b00010) { // ASRS imm
                uint8_t imm5 = (hw >> 6) & 0x1F;
                uint8_t Rm   = (hw >> 3) & 0x7;
                uint8_t Rd   = hw & 0x7;
                uint8_t amt  = imm5 ? imm5 : 32;
                bool cy;
                R[Rd] = shift(R[Rm], 2, amt, C(), &cy);
                setC(cy); update_nz(R[Rd]);
            } else {
                goto unimplemented;
            }
        } else {
            uint8_t op15_11 = hw >> 11;
            if (op15_11 == 0b00011) {
                // ADD/SUB register or imm3 T1
                uint8_t sub     = (hw >> 9) & 1;
                uint8_t use_imm = (hw >> 10) & 1;
                uint8_t Rm_imm3 = (hw >> 6) & 0x7;
                uint8_t Rn      = (hw >> 3) & 0x7;
                uint8_t Rd      = hw & 0x7;
                uint32_t b      = use_imm ? Rm_imm3 : R[Rm_imm3];
                uint32_t res; bool co, vo;
                if (!sub) add_with_carry(R[Rn], b, false, res, co, vo);
                else      add_with_carry(R[Rn], ~b, true,  res, co, vo);
                R[Rd] = res; update_nz(res); setC(co); setV(vo);
            } else {
                // MOVS/CMP/ADDS/SUBS immediate  (bits[15:13]=001, op15_11 ∈ 4..7)
                uint8_t op_imm = op15_11 & 0x3; // bits[12:11]
                uint8_t Rdn    = (hw >> 8) & 0x7;
                uint8_t imm8   = hw & 0xFF;
                uint32_t res; bool co, vo;
                switch (op_imm) {
                case 0: R[Rdn] = imm8; update_nz(R[Rdn]); break; // MOVS
                case 1: // CMP
                    add_with_carry(R[Rdn], ~(uint32_t)imm8, true, res, co, vo);
                    update_nz(res); setC(co); setV(vo); break;
                case 2: // ADDS
                    add_with_carry(R[Rdn], imm8, false, res, co, vo);
                    R[Rdn] = res; update_nz(res); setC(co); setV(vo); break;
                case 3: // SUBS
                    add_with_carry(R[Rdn], ~(uint32_t)imm8, true, res, co, vo);
                    R[Rdn] = res; update_nz(res); setC(co); setV(vo); break;
                }
            }
        }
        return;
    }

    // ── Data processing ────────────────────────────────────────────────────
    data_proc:
    if ((hw >> 10) == 0b010000) {
        uint8_t op4 = (hw >> 6) & 0xF;
        uint8_t Rm  = (hw >> 3) & 0x7;
        uint8_t Rdn = hw & 0x7;
        uint32_t res; bool co, vo;
        switch (op4) {
        case 0x0: // AND
            R[Rdn] &= R[Rm]; update_nz(R[Rdn]); break;
        case 0x1: // EOR
            R[Rdn] ^= R[Rm]; update_nz(R[Rdn]); break;
        case 0x2: // LSL (reg)
            { bool cy; R[Rdn] = shift(R[Rdn], 0, R[Rm]&0xFF, C(), &cy);
              if (R[Rm]&0xFF) setC(cy); update_nz(R[Rdn]); break; }
        case 0x3: // LSR (reg)
            { bool cy; R[Rdn] = shift(R[Rdn], 1, R[Rm]&0xFF, C(), &cy);
              if (R[Rm]&0xFF) setC(cy); update_nz(R[Rdn]); break; }
        case 0x4: // ASR (reg)
            { bool cy; R[Rdn] = shift(R[Rdn], 2, R[Rm]&0xFF, C(), &cy);
              if (R[Rm]&0xFF) setC(cy); update_nz(R[Rdn]); break; }
        case 0x5: // ADC
            add_with_carry(R[Rdn], R[Rm], C(), res, co, vo);
            R[Rdn] = res; update_nz(res); setC(co); setV(vo); break;
        case 0x6: // SBC
            add_with_carry(R[Rdn], ~R[Rm], C(), res, co, vo);
            R[Rdn] = res; update_nz(res); setC(co); setV(vo); break;
        case 0x7: // ROR (reg)
            { bool cy; R[Rdn] = shift(R[Rdn], 3, R[Rm]&0xFF, C(), &cy);
              if (R[Rm]&0xFF) setC(cy); update_nz(R[Rdn]); break; }
        case 0x8: // TST
            res = R[Rdn] & R[Rm]; update_nz(res); break;
        case 0x9: // RSB (NEG)
            add_with_carry(0, ~R[Rm], true, res, co, vo);
            R[Rdn] = res; update_nz(res); setC(co); setV(vo); break;
        case 0xA: // CMP
            add_with_carry(R[Rdn], ~R[Rm], true, res, co, vo);
            update_nz(res); setC(co); setV(vo); break;
        case 0xB: // CMN
            add_with_carry(R[Rdn], R[Rm], false, res, co, vo);
            update_nz(res); setC(co); setV(vo); break;
        case 0xC: // ORR
            R[Rdn] |= R[Rm]; update_nz(R[Rdn]); break;
        case 0xD: // MUL
            R[Rdn] = R[Rdn] * R[Rm]; update_nz(R[Rdn]); break;
        case 0xE: // BIC
            R[Rdn] &= ~R[Rm]; update_nz(R[Rdn]); break;
        case 0xF: // MVN
            R[Rdn] = ~R[Rm]; update_nz(R[Rdn]); break;
        }
        return;
    }

    // ── Special data / BX / BLX ───────────────────────────────────────────
    special_branch:
    if ((hw >> 10) == 0b010001) {
        uint8_t op = (hw >> 8) & 0x3;
        uint8_t DN = (hw >> 7) & 1;
        uint8_t Rm = (hw >> 3) & 0xF;
        uint8_t Rdn = (DN << 3) | (hw & 0x7);
        if (op == 0) { // ADD (high reg)
            uint32_t res = R[Rdn] + R[Rm];
            if (Rdn == PC) { branch_to(res); }
            else R[Rdn] = res;
        } else if (op == 1) { // CMP (high reg)
            uint32_t res; bool co, vo;
            add_with_carry(R[Rdn], ~R[Rm], true, res, co, vo);
            update_nz(res); setC(co); setV(vo);
        } else if (op == 2) { // MOV (high reg)
            if (Rdn == PC) { branch_to(R[Rm]); }
            else R[Rdn] = R[Rm];
        } else { // BX / BLX
            uint32_t addr = R[Rm];
            if ((hw >> 7) & 1) { // BLX
                R[LR] = (ia + 2) | 1; // return address
                blx_to(addr);
            } else { // BX
                branch_to(addr);
            }
        }
        return;
    }

    // ── LDR literal (PC-relative) ──────────────────────────────────────────
    if ((hw >> 11) == 0b01001) {
        uint8_t Rt   = (hw >> 8) & 0x7;
        uint8_t imm8 = hw & 0xFF;
        uint32_t base = (R[PC] & ~3u); // already ia+4, word-aligned
        R[Rt] = mem.read32(base + imm8 * 4);
        return;
    }

    // ── Load/Store register offset (010 1xx) ──────────────────────────────
    if ((hw >> 12) == 0b0101) {
        uint8_t op3  = (hw >> 9) & 0x7;
        uint8_t Rm   = (hw >> 6) & 0x7;
        uint8_t Rn   = (hw >> 3) & 0x7;
        uint8_t Rt   = hw & 0x7;
        uint32_t addr = R[Rn] + R[Rm];
        switch (op3) {
        case 0: mem.write32(addr, R[Rt]); break;       // STR
        case 1: mem.write16(addr, (uint16_t)R[Rt]); break; // STRH
        case 2: mem.write8(addr, (uint8_t)R[Rt]); break;   // STRB
        case 3: R[Rt] = (int32_t)(int8_t)mem.read8(addr); break;  // LDRSB
        case 4: R[Rt] = mem.read32(addr); break;       // LDR
        case 5: R[Rt] = mem.read16(addr); break;       // LDRH
        case 6: R[Rt] = mem.read8(addr); break;        // LDRB
        case 7: R[Rt] = (int32_t)(int16_t)mem.read16(addr); break; // LDRSH
        }
        return;
    }

    // ── Load/Store word/byte immediate (011x xx) ──────────────────────────
    if ((hw >> 13) == 0b011) {
        bool B   = (hw >> 12) & 1; // 0=word, 1=byte
        bool L   = (hw >> 11) & 1; // 0=store, 1=load
        uint8_t imm5 = (hw >> 6) & 0x1F;
        uint8_t Rn   = (hw >> 3) & 0x7;
        uint8_t Rt   = hw & 0x7;
        uint32_t addr = R[Rn] + (B ? imm5 : imm5 * 4u);
        if (!L && !B) mem.write32(addr, R[Rt]);
        else if (!L && B) mem.write8(addr, (uint8_t)R[Rt]);
        else if (L && !B) R[Rt] = mem.read32(addr);
        else              R[Rt] = mem.read8(addr);
        return;
    }

    // ── Load/Store halfword immediate (1000 xx) ───────────────────────────
    if ((hw >> 12) == 0b1000) {
        bool L       = (hw >> 11) & 1;
        uint8_t imm5 = (hw >> 6) & 0x1F;
        uint8_t Rn   = (hw >> 3) & 0x7;
        uint8_t Rt   = hw & 0x7;
        uint32_t addr = R[Rn] + imm5 * 2u;
        if (!L) mem.write16(addr, (uint16_t)R[Rt]);
        else    R[Rt] = mem.read16(addr);
        return;
    }

    // ── Load/Store SP-relative (1001 xx) ──────────────────────────────────
    if ((hw >> 12) == 0b1001) {
        bool L      = (hw >> 11) & 1;
        uint8_t Rt  = (hw >> 8) & 0x7;
        uint8_t imm8 = hw & 0xFF;
        uint32_t addr = R[SP] + imm8 * 4u;
        if (!L) mem.write32(addr, R[Rt]);
        else    R[Rt] = mem.read32(addr);
        return;
    }

    // ── ADR / ADD SP+imm (1010 xx) ────────────────────────────────────────
    if ((hw >> 12) == 0b1010) {
        bool sp_rel  = (hw >> 11) & 1;
        uint8_t Rd   = (hw >> 8) & 0x7;
        uint8_t imm8 = hw & 0xFF;
        if (!sp_rel) R[Rd] = (R[PC] & ~3u) + imm8 * 4u; // ADR (PC-relative)
        else         R[Rd] = R[SP] + imm8 * 4u;          // ADD Rd, SP, #imm
        return;
    }

    // ── Miscellaneous 16-bit (1011 xx) ────────────────────────────────────
    if ((hw >> 12) == 0b1011) {
        uint8_t op8 = (hw >> 8) & 0xF; // bits[11:8]

        // ADD/SUB SP ±imm7 (1011 0000 x xxxxxxx)
        if ((hw >> 8) == 0b10110000) {
            uint8_t sub  = (hw >> 7) & 1;
            uint8_t imm7 = hw & 0x7F;
            if (!sub) R[SP] += imm7 * 4u;
            else      R[SP] -= imm7 * 4u;
            return;
        }

        // CBZ / CBNZ
        if ((hw & 0xF500) == 0xB100) {
            uint8_t op_n  = (hw >> 11) & 1; // 0=CBZ, 1=CBNZ
            uint8_t i     = (hw >> 9) & 1;
            uint8_t imm5  = (hw >> 3) & 0x1F;
            uint8_t Rn    = hw & 0x7;
            uint32_t imm  = (i << 6) | (imm5 << 1);
            bool taken = op_n ? (R[Rn] != 0) : (R[Rn] == 0);
            if (taken) branch_to(R[PC] + imm); // R[PC] = ia+4
            return;
        }

        // SXTH/SXTB/UXTH/UXTB (1011 0010)
        if ((hw >> 8) == 0b10110010) {
            uint8_t op2 = (hw >> 6) & 0x3;
            uint8_t Rm  = (hw >> 3) & 0x7;
            uint8_t Rd  = hw & 0x7;
            switch (op2) {
            case 0: R[Rd] = (int32_t)(int16_t)R[Rm]; break; // SXTH
            case 1: R[Rd] = (int32_t)(int8_t)R[Rm];  break; // SXTB
            case 2: R[Rd] = (uint16_t)R[Rm]; break;         // UXTH
            case 3: R[Rd] = (uint8_t)R[Rm];  break;         // UXTB
            }
            return;
        }

        // REV (1011 1010)
        if ((hw >> 8) == 0b10111010) {
            uint8_t op2 = (hw >> 6) & 0x3;
            uint8_t Rm  = (hw >> 3) & 0x7;
            uint8_t Rd  = hw & 0x7;
            uint32_t v = R[Rm];
            if (op2 == 0) { // REV
                R[Rd] = ((v&0xFF)<<24)|((v&0xFF00)<<8)|((v>>8)&0xFF00)|((v>>24)&0xFF);
            } else if (op2 == 1) { // REV16
                R[Rd] = ((v&0xFF)<<8)|((v>>8)&0xFF)|((v&0xFF0000)<<8)|((v>>8)&0xFF0000);
            } else if (op2 == 3) { // REVSH
                R[Rd] = (int32_t)(int16_t)(((v&0xFF)<<8)|((v>>8)&0xFF));
            }
            return;
        }

        // PUSH (1011 0 1 0 M reglist)
        if ((hw & 0xFE00) == 0xB400) {
            uint16_t reglist = hw & 0xFF;
            if ((hw >> 8) & 1) reglist |= (1 << LR); // M bit = push LR
            push_regs(reglist);
            return;
        }

        // POP (1011 1 1 0 P reglist)
        if ((hw & 0xFE00) == 0xBC00) {
            uint16_t reglist = hw & 0xFF;
            if ((hw >> 8) & 1) reglist |= (1 << PC); // P bit = pop PC
            pop_regs(reglist);
            return;
        }

        // BKPT
        if ((hw >> 8) == 0b10111110) {
            std::cout << "[CPU] BKPT #" << (hw & 0xFF) << " at 0x"
                      << std::hex << ia << std::endl;
            halted = true; halt_pc = ia; return;
        }

        // Hints: NOP, WFI, etc.
        if ((hw >> 8) == 0b10111111) {
            // NOP, WFI, etc. – just ignore
            return;
        }

        // CPSID/CPSIE (1011 0110 0101 xxxx)
        if ((hw & 0xFF30) == 0xB610) {
            bool disable = (hw >> 2) & 1;
            if (disable) PRIMASK |= 1;
            else         PRIMASK &= ~1u;
            return;
        }

        goto unimplemented;
    }

    // ── STM / LDM (1100 xx) ───────────────────────────────────────────────
    if ((hw >> 12) == 0b1100) {
        bool L       = (hw >> 11) & 1;
        uint8_t Rn   = (hw >> 8) & 0x7;
        uint16_t rl  = hw & 0xFF;
        if (!L) stm(Rn, rl, true);
        else    ldm(Rn, rl, true);
        return;
    }

    // ── Conditional branch / SVC (1101 xx) ───────────────────────────────
    if ((hw >> 12) == 0b1101) {
        uint8_t cond = (hw >> 8) & 0xF;
        if (cond == 0xE || cond == 0xF) goto unimplemented; // UDF/SVC
        if (cond_check(cond)) {
            int32_t offset = sign_extend(hw & 0xFF, 8) * 2;
            branch_to(R[PC] + offset); // R[PC] = ia+4
        }
        return;
    }

    // ── Unconditional branch (1110 0x) ────────────────────────────────────
    if ((hw >> 11) == 0b11100) {
        int32_t offset = sign_extend(hw & 0x7FF, 11) * 2;
        branch_to(R[PC] + offset);
        return;
    }

    unimplemented:
    ++unimpl_count;
    std::cout << "[CPU] UNIMPL 16-bit 0x" << std::hex << std::setw(4)
              << std::setfill('0') << hw << " at 0x" << ia << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// 32-bit Thumb-2 instruction executor
// ──────────────────────────────────────────────────────────────────────────────

void CPU::exec32(uint32_t hw, uint32_t ia) {
    // R[PC] = ia+4 already set by step()
    uint8_t hw1h = hw >> 28;     // bits[31:28]
    uint8_t op1  = (hw >> 27) & 0x1F; // bits[31:27]... we'll decode by pattern

    // ── BL / BLX / B.W / B.cond.W (11110... with hw2 bit15=1) ──────────────
    // bit15=1 selects branches; bit15=0 selects data-processing (handled below)
    if ((hw >> 27) == 0b11110 && ((hw >> 15) & 1)) {
        // BL T1: 11110 S imm10 | 11 J1 1 J2 imm11
        // BLX:   11110 S imm10 | 11 J1 0 J2 imm11
        uint32_t S    = (hw >> 26) & 1;
        uint32_t imm10= (hw >> 16) & 0x3FF;
        uint32_t J1   = (hw >> 13) & 1;
        bool bit12    = (hw >> 12) & 1;
        uint32_t J2   = (hw >> 11) & 1;
        uint32_t imm11= hw & 0x7FF;

        uint32_t I1 = (J1 ^ S) ^ 1;
        uint32_t I2 = (J2 ^ S) ^ 1;
        uint32_t off32 = (S << 24) | (I1 << 23) | (I2 << 22) |
                         (imm10 << 12) | (imm11 << 1);
        if (S) off32 |= 0xFF000000u;
        int32_t offset = (int32_t)off32;

        // hw2 bits[15:14]: 11=BL, 10=B or B.cond
        uint8_t hw2_op = (hw >> 14) & 0x3;
        if (hw2_op == 0b11) {
            if (bit12) { // BL
                R[LR] = (ia + 4) | 1;
                branch_to(R[PC] + offset);
            } else { // BLX imm
                R[LR] = (ia + 4) | 1;
                branch_to((R[PC] & ~3u) + (imm10 << 12) + (imm11 << 1));
            }
        } else {
            // B.W T4 (bit12=1): unconditional, same 25-bit offset as BL
            // B.cond.W T3 (bit12=0): conditional, 21-bit offset with cond field
            if (bit12) {
                branch_to(R[PC] + offset);
            } else {
                uint8_t cond  = (hw >> 22) & 0xF;
                uint32_t S3   = (hw >> 26) & 1;
                uint32_t imm6 = (hw >> 16) & 0x3F;
                uint32_t off21 = (S3<<20)|(J2<<19)|(J1<<18)|(imm6<<12)|(imm11<<1);
                if (S3) off21 |= 0xFFE00000u;
                if (cond_check(cond)) branch_to(R[PC] + (int32_t)off21);
            }
        }
        return;
    }

    // ── LDM/STM / PUSH/POP Thumb-2 (1110 100x, bit22=0) ─────────────────
    // bit22=1 selects STRD/LDRD (handled below); bit22=0 selects LDM/STM
    if ((hw >> 25) == 0b1110100 && !((hw >> 22) & 1)) {
        // E8xx / E9xx / EAxx / EBxx
        bool L  = (hw >> 20) & 1;
        bool P  = (hw >> 24) & 1;
        bool U  = (hw >> 23) & 1;
        bool W  = (hw >> 21) & 1;
        uint8_t Rn = (hw >> 16) & 0xF;
        uint16_t rl = hw & 0xFFFF;

        // PUSH T2 = STMDB SP!, reglist (op1=E92D)
        if (!L && Rn == SP && !U && W) { // STMDB SP!
            push_regs(rl); return;
        }
        // POP T2 = LDMIA SP!, reglist (op1=E8BD)
        if (L && Rn == SP && U && W) {
            pop_regs(rl); return;
        }
        // LDM/STM
        if (!P) { // LDMIA/STMIA
            if (!L) stm(Rn, rl, W);
            else    ldm(Rn, rl, W);
            return;
        }
        // STMDB / LDMDB
        if (P && !U) {
            // Decrement before
            int count = 0;
            for (int i = 0; i < 16; ++i) if (rl & (1<<i)) ++count;
            uint32_t addr = R[Rn] - count * 4;
            uint32_t start = addr;
            for (int i = 0; i <= 15; ++i) {
                if (rl & (1<<i)) {
                    if (!L) mem.write32(addr, R[i]);
                    else {
                        uint32_t v = mem.read32(addr);
                        if (i == PC) { R[PC] = v & ~1u; pc_written = true; }
                        else R[i] = v;
                    }
                    addr += 4;
                }
            }
            if (W) R[Rn] = start;
            return;
        }
        goto unimpl32;
    }

    // ── STRD / LDRD (1110 100x 1 P U) ───────────────────────────────────
    // E9C2 5500 → strd r5,r5,[r2]
    if ((hw >> 24) == 0b11101001 || (hw >> 24) == 0b11101000) {
        bool L  = (hw >> 20) & 1;
        bool P  = (hw >> 24) & 1;
        bool U  = (hw >> 23) & 1;
        bool W  = (hw >> 21) & 1;
        uint8_t Rn  = (hw >> 16) & 0xF;
        uint8_t Rt  = (hw >> 12) & 0xF;
        uint8_t Rt2 = (hw >> 8)  & 0xF;
        uint8_t imm8 = hw & 0xFF;
        uint32_t off = imm8 * 4;
        uint32_t addr = U ? R[Rn] + off : R[Rn] - off;
        if (P) { // offset/pre-index
            if (!L) { mem.write32(addr, R[Rt]); mem.write32(addr+4, R[Rt2]); }
            else    { R[Rt] = mem.read32(addr); R[Rt2] = mem.read32(addr+4); }
            if (W) R[Rn] = addr;
        } else { // post-index
            uint32_t base = R[Rn];
            if (!L) { mem.write32(base, R[Rt]); mem.write32(base+4, R[Rt2]); }
            else    { R[Rt] = mem.read32(base); R[Rt2] = mem.read32(base+4); }
            R[Rn] = addr;
        }
        return;
    }

    // ── Data processing (shifted register) 1110 101x ─────────────────────
    if ((hw >> 25) == 0b1110101) {
        uint8_t op4  = (hw >> 21) & 0xF;
        uint8_t S    = (hw >> 20) & 1;
        uint8_t Rn   = (hw >> 16) & 0xF;
        uint8_t Rd   = (hw >> 8)  & 0xF;
        uint8_t imm3 = (hw >> 12) & 0x7;
        uint8_t type = (hw >> 4)  & 0x3;
        uint8_t imm2 = (hw >> 6)  & 0x3;
        uint8_t Rm   = hw & 0xF;
        uint8_t imm5 = (imm3 << 2) | imm2;

        bool cy;
        uint32_t shifted = shift(R[Rm], type, imm5, C(), &cy);
        uint32_t res; bool co = cy, vo;

        switch (op4) {
        case 0x0: // AND / TST
            res = R[Rn] & shifted;
            if (Rd != 0xF) { R[Rd] = res; if (S) { update_nz(res); if (imm5) setC(cy); } }
            else if (S)    { update_nz(res); if (imm5) setC(cy); }
            break;
        case 0x1: // BIC
            res = R[Rn] & ~shifted;
            R[Rd] = res; if (S) { update_nz(res); if (imm5) setC(cy); }
            break;
        case 0x2: // ORR / MOV
            if (Rn == 0xF) res = shifted; // MOV
            else res = R[Rn] | shifted;
            R[Rd] = res;
            if (S) { update_nz(res); if (imm5) setC(cy); }
            if (Rd == PC) { branch_to(res); }
            break;
        case 0x3: // ORN / MVN
            if (Rn == 0xF) res = ~shifted;
            else res = R[Rn] | ~shifted;
            R[Rd] = res; if (S) { update_nz(res); }
            break;
        case 0x4: // EOR / TEQ
            res = R[Rn] ^ shifted;
            if (Rd != 0xF) { R[Rd] = res; if (S) { update_nz(res); if (imm5) setC(cy); } }
            else if (S) { update_nz(res); if (imm5) setC(cy); }
            break;
        case 0x8: // ADD / CMN (Rd=0xF,S=1 → CMN: flags only, no branch)
            add_with_carry(R[Rn], shifted, false, res, co, vo);
            if (Rd == 0xF && S) { update_nz(res); setC(co); setV(vo); }
            else { R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
                   if (Rd == PC) branch_to(res); }
            break;
        case 0xA: // ADC
            add_with_carry(R[Rn], shifted, C(), res, co, vo);
            R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        case 0xB: // SBC
            add_with_carry(R[Rn], ~shifted, C(), res, co, vo);
            R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        case 0xD: // SUB / CMP (Rd=0xF,S=1 → CMP: flags only, no branch)
            add_with_carry(R[Rn], ~shifted, true, res, co, vo);
            if (Rd == 0xF && S) { update_nz(res); setC(co); setV(vo); }
            else { R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
                   if (Rd == PC) branch_to(res); }
            break;
        case 0xE: // RSB
            add_with_carry(~R[Rn], shifted, true, res, co, vo);
            R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        default:
            goto unimpl32;
        }
        return;
    }

    // ── Data processing (modified immediate) 1111 0x0x ────────────────────
    if ((hw >> 27) == 0b11110 && !((hw >> 25) & 1)) {
        uint8_t op4  = (hw >> 21) & 0xF;
        uint8_t S    = (hw >> 20) & 1;
        uint8_t Rn   = (hw >> 16) & 0xF;
        uint8_t Rd   = (hw >> 8) & 0xF;
        uint16_t imm12 = ((hw >> 15) & 0x800) | ((hw >> 4) & 0x700) | (hw & 0xFF);
        bool cy;
        uint32_t imm = thumb_expand_imm(imm12, &cy);
        uint32_t res; bool co = false, vo = false;

        switch (op4) {
        case 0x0: // AND / TST(Rd=1111)
            res = R[Rn] & imm;
            if (Rd != 0xF) { R[Rd] = res; if (S) { update_nz(res); if (cy) setC(cy); } }
            else if (S)    { update_nz(res); }
            break;
        case 0x1: // BIC
            res = R[Rn] & ~imm;
            R[Rd] = res; if (S) update_nz(res);
            break;
        case 0x2: // ORR / MOV
            if (Rn == 0xF) res = imm; // MOV
            else res = R[Rn] | imm;
            R[Rd] = res; if (S) update_nz(res);
            break;
        case 0x3: // ORN / MVN
            if (Rn == 0xF) res = ~imm;
            else res = R[Rn] | ~imm;
            R[Rd] = res; if (S) update_nz(res);
            break;
        case 0x4: // EOR / TEQ
            res = R[Rn] ^ imm;
            if (Rd != 0xF) { R[Rd] = res; if (S) update_nz(res); }
            else if (S) update_nz(res);
            break;
        case 0x8: // ADD / CMN
            add_with_carry(R[Rn], imm, false, res, co, vo);
            if (Rd != 0xF) { R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); } }
            else if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        case 0xA: // ADC
            add_with_carry(R[Rn], imm, C(), res, co, vo);
            R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        case 0xB: // SBC
            add_with_carry(R[Rn], ~imm, C(), res, co, vo);
            R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        case 0xD: // SUB / CMP
            add_with_carry(R[Rn], ~imm, true, res, co, vo);
            if (Rd != 0xF) { R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); } }
            else if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        case 0xE: // RSB
            add_with_carry(~R[Rn], imm, true, res, co, vo);
            R[Rd] = res; if (S) { update_nz(res); setC(co); setV(vo); }
            break;
        default:
            goto unimpl32;
        }
        return;
    }

    // ── Data processing (plain immediate, 1111 0x1x) ─────────────────────
    // MOVW, MOVT, ADD/SUB with 12-bit imm, etc.
    if (((hw >> 27) == 0b11110) && ((hw >> 25) & 1)) {
        uint8_t op4  = (hw >> 20) & 0x1F; // bits[24:20]
        uint8_t Rn   = (hw >> 16) & 0xF;
        uint8_t Rd   = (hw >> 8)  & 0xF;

        // MOVW T3: 0 10 1 00 imm4 | 0 imm3 Rd imm8
        if ((op4 & 0b11111) == 0b00100) { // ADR T2/T3 / ADD imm12
            // ADD Rd, Rn, #imm12 (T4) or ADDW
            uint16_t imm = ((hw >> 15) & 0x800) | ((hw >> 4) & 0x700) | (hw & 0xFF);
            R[Rd] = R[Rn] + imm;
            return;
        }
        if ((op4 & 0b11111) == 0b01010) { // SUB Rd, Rn, #imm12 (T4)
            uint16_t imm = ((hw >> 15) & 0x800) | ((hw >> 4) & 0x700) | (hw & 0xFF);
            R[Rd] = R[Rn] - imm;
            return;
        }

        // MOVW (move wide immediate, 16-bit zero-extend)
        if ((hw & 0xFBF08000u) == 0xF2400000u) {
            uint8_t imm4  = (hw >> 16) & 0xF;
            uint8_t i     = (hw >> 26) & 1;
            uint8_t imm3  = (hw >> 12) & 0x7;
            uint8_t imm8  = hw & 0xFF;
            R[Rd] = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
            return;
        }
        // MOVT (move top)
        if ((hw & 0xFBF08000u) == 0xF2C00000u) {
            uint8_t imm4  = (hw >> 16) & 0xF;
            uint8_t i     = (hw >> 26) & 1;
            uint8_t imm3  = (hw >> 12) & 0x7;
            uint8_t imm8  = hw & 0xFF;
            uint16_t imm16 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
            R[Rd] = (R[Rd] & 0x0000FFFFu) | ((uint32_t)imm16 << 16);
            return;
        }
        // UBFX T1: op4=11100  extract width bits from Rn starting at lsb, zero-extend
        if (op4 == 0b11100) {
            uint8_t imm3     = (hw >> 12) & 0x7;
            uint8_t imm2     = (hw >> 6)  & 0x3;
            uint8_t widthm1  = hw & 0x1F;
            uint8_t lsb      = (imm3 << 2) | imm2;
            uint8_t width    = widthm1 + 1;
            R[Rd] = (R[Rn] >> lsb) & ((1u << width) - 1);
            return;
        }
        // SBFX T1: op4=10100  same but sign-extend
        if (op4 == 0b10100) {
            uint8_t imm3     = (hw >> 12) & 0x7;
            uint8_t imm2     = (hw >> 6)  & 0x3;
            uint8_t widthm1  = hw & 0x1F;
            uint8_t lsb      = (imm3 << 2) | imm2;
            uint8_t width    = widthm1 + 1;
            uint32_t v = (R[Rn] >> lsb) & ((1u << width) - 1);
            if (v & (1u << (width - 1))) v |= ~((1u << width) - 1); // sign-extend
            R[Rd] = v;
            return;
        }
        // BFI/BFC T1: op4=10110  bit-field insert (Rn≠1111) / clear (Rn=1111)
        if (op4 == 0b10110) {
            uint8_t imm3    = (hw >> 12) & 0x7;
            uint8_t imm2    = (hw >> 6)  & 0x3;
            uint8_t msb     = hw & 0x1F;
            uint8_t lsb     = (imm3 << 2) | imm2;
            uint8_t width   = msb - lsb + 1;
            uint32_t mask   = ((1u << width) - 1) << lsb;
            if (Rn == 0xF) R[Rd] &= ~mask; // BFC
            else           R[Rd] = (R[Rd] & ~mask) | ((R[Rn] << lsb) & mask); // BFI
            return;
        }
        goto unimpl32;
    }

    // ─────────────────────────────────────────────────────────────────────
    // 0xF8xx / 0xF9xx / 0xFBxx / 0xFCxx load/store and multiply
    {
        uint8_t top8 = hw >> 24;

        // LDR/STR/LDRB/STRB register-based (offset / pre / post) ─────────
        // 0xF8xx family
        if ((top8 & 0xFE) == 0xF8) {
            bool L    = (hw >> 20) & 1;
            uint8_t sz = (hw >> 21) & 0x3; // 00=byte,01=half,10=word,11=dword?
            // Top bits: 1111 1000 = 0xF8, 1111 1001 = 0xF9
            // Distinguish by bit 23 (U), bit 22 (size), bit 21 (L)...
            // Actually F8xx encoding:
            // bits[27:20] decode the type
        }

        // ── STR/LDR (immediate 12-bit positive, T3) ──────────────────────
        // STR.W T3: 1111 1000 1100 Rn | Rt imm12    (0xF8Cx xxxx)
        // LDR.W T3: 1111 1000 1101 Rn | Rt imm12    (0xF8Dx xxxx)
        // STRB.W T2: 1111 1000 1000 Rn | Rt imm12   (0xF88x xxxx)
        // LDRB.W T2: 1111 1000 1001 Rn | Rt imm12   (0xF89x xxxx)
        // STRH.W: 1111 1000 1010 Rn | Rt imm12      (0xF8Ax xxxx)
        // LDRH.W: 1111 1000 1011 Rn | Rt imm12      (0xF8Bx xxxx)
        if ((hw >> 23) == 0b111110001) {
            uint8_t sz = (hw >> 21) & 0x3;
            bool L     = (hw >> 20) & 1;
            uint8_t Rn = (hw >> 16) & 0xF;
            uint8_t Rt = (hw >> 12) & 0xF;
            uint16_t imm12 = hw & 0xFFF;
            uint32_t addr = R[Rn] + imm12;
            goto ls_common;

            ls_common:
            if (!L) {
                switch (sz) {
                case 0: mem.write8(addr, (uint8_t)R[Rt]); break;
                case 1: mem.write16(addr, (uint16_t)R[Rt]); break;
                case 2: mem.write32(addr, R[Rt]); break;
                }
            } else {
                switch (sz) {
                case 0: R[Rt] = mem.read8(addr); break;
                case 1: R[Rt] = mem.read16(addr); break;
                case 2: R[Rt] = mem.read32(addr);
                    if (Rt == PC) { R[PC] = R[Rt] & ~1u; pc_written = true; }
                    break;
                }
            }
            return;
        }

        // ── STR/LDR (immediate 8-bit with P/U/W) T4 ─────────────────────
        // e.g. F803 2B01 = STRB.W r2,[r3],#1 (post-index)
        // F842 5B04 = STR.W r5,[r2],#4
        // F855 3B04 = LDR.W r3,[r5],#4
        // F85D FB04 = LDR.W pc,[sp],#4
        if (((hw >> 24) & 0xFF) == 0xF8 ||
            ((hw >> 24) & 0xFF) == 0xF9) {
            bool sign  = (hw >> 24) & 1; // 0=unsigned,1=signed-ext
            uint8_t sz = (hw >> 21) & 0x3;
            bool L     = (hw >> 20) & 1;
            uint8_t Rn = (hw >> 16) & 0xF;
            uint8_t Rt = (hw >> 12) & 0xF;
            // T4 (8-bit imm): bit11=1, bit10=P, bit9=U, bit8=W
            if ((hw >> 11) & 1) {
                bool P    = (hw >> 10) & 1;
                bool U    = (hw >> 9)  & 1;
                bool W    = (hw >> 8)  & 1;
                uint8_t imm8 = hw & 0xFF;
                uint32_t offset = imm8;
                uint32_t addr = P ? (U ? R[Rn]+offset : R[Rn]-offset) : R[Rn];
                if (!L) {
                    switch (sz) {
                    case 0: mem.write8(addr, (uint8_t)R[Rt]); break;
                    case 1: mem.write16(addr, (uint16_t)R[Rt]); break;
                    case 2: mem.write32(addr, R[Rt]); break;
                    }
                } else {
                    uint32_t v = 0;
                    switch (sz) {
                    case 0: v = sign ? (uint32_t)(int32_t)(int8_t)mem.read8(addr)   : mem.read8(addr);  break;
                    case 1: v = sign ? (uint32_t)(int32_t)(int16_t)mem.read16(addr) : mem.read16(addr); break;
                    case 2: v = mem.read32(addr); break;
                    }
                    if (Rt == PC) { R[PC] = v & ~1u; pc_written = true; }
                    else R[Rt] = v;
                }
                if (W || !P) { // writeback or post-index
                    uint32_t wb = !P ? (U ? R[Rn]+offset : R[Rn]-offset) : addr;
                    R[Rn] = wb;
                }
                return;
            } else {
                // T2: Register offset  addr = R[Rn] + (R[Rm] << shift_imm)
                uint8_t shift_imm = (hw >> 4) & 0x3;
                uint8_t Rm = hw & 0xF;
                uint32_t addr = R[Rn] + (R[Rm] << shift_imm);
                if (!L) {
                    switch (sz) {
                    case 0: mem.write8(addr, (uint8_t)R[Rt]); break;
                    case 1: mem.write16(addr, (uint16_t)R[Rt]); break;
                    case 2: mem.write32(addr, R[Rt]); break;
                    }
                } else {
                    uint32_t v = 0;
                    switch (sz) {
                    case 0: v = sign ? (uint32_t)(int32_t)(int8_t)mem.read8(addr)   : mem.read8(addr);  break;
                    case 1: v = sign ? (uint32_t)(int32_t)(int16_t)mem.read16(addr) : mem.read16(addr); break;
                    case 2: v = mem.read32(addr); break;
                    }
                    if (Rt == PC) { R[PC] = v & ~1u; pc_written = true; }
                    else R[Rt] = v;
                }
                return;
            }
        }

        // ── Shift/rotate by register (0xFAxx) ────────────────────────────
        // LSL/LSR/ASR/ROR T2: 1111 1010 0 op S Rn | 1111 Rd 0000 Rm
        if ((hw >> 24) == 0xFA) {
            uint8_t op2 = (hw >> 21) & 0x3; // 00=LSL,01=LSR,10=ASR,11=ROR
            bool    S   = (hw >> 20) & 1;
            uint8_t Rn  = (hw >> 16) & 0xF;
            uint8_t Rd  = (hw >> 8)  & 0xF;
            uint8_t Rm  = hw & 0xF;
            bool cy;
            uint8_t amt = R[Rm] & 0xFF;
            R[Rd] = shift(R[Rn], op2, amt, C(), &cy);
            if (S) { if (amt) setC(cy); update_nz(R[Rd]); }
            return;
        }

        // ── Multiply/Divide (0xFBxx) ──────────────────────────────────────
        if ((hw >> 24) == 0xFB) {
            uint8_t op1 = (hw >> 20) & 0xF;
            uint8_t Rn  = (hw >> 16) & 0xF;
            uint8_t Ra  = (hw >> 12) & 0xF;
            uint8_t Rd  = (hw >> 8)  & 0xF;
            uint8_t op2 = (hw >> 4)  & 0xF;
            uint8_t Rm  = hw & 0xF;

            if (op1 == 0x0 && op2 == 0x0) { // MUL T2
                R[Rd] = R[Rn] * R[Rm];
                return;
            }
            if (op1 == 0x0 && op2 != 0xF) { // MLA / MLS
                if (Ra != 0xF) {
                    if ((hw >> 4 & 0xF) == 0) R[Rd] = R[Rn]*R[Rm] + R[Ra]; // MLA
                    else R[Rd] = R[Ra] - R[Rn]*R[Rm]; // MLS
                } else R[Rd] = R[Rn] * R[Rm];
                return;
            }
            if (op1 == 0xA) { // UMULL T1
                uint64_t r = (uint64_t)R[Rn] * R[Rm];
                R[Ra] = (uint32_t)r;         // RdLo
                R[Rd] = (uint32_t)(r >> 32); // RdHi
                return;
            }
            if (op1 == 0xB) { // UDIV T1
                if (R[Rm] == 0) R[Rd] = 0;
                else R[Rd] = R[Rn] / R[Rm];
                return;
            }
            if (op1 == 0x9) { // SDIV T1
                if (R[Rm] == 0) R[Rd] = 0;
                else R[Rd] = (uint32_t)((int32_t)R[Rn] / (int32_t)R[Rm]);
                return;
            }
            if (op1 == 0xC) { // SMULL
                int64_t r = (int64_t)(int32_t)R[Rn] * (int32_t)R[Rm];
                R[Ra] = (uint32_t)r;
                R[Rd] = (uint32_t)((uint64_t)r >> 32);
                return;
            }
            if (op1 == 0xE) { // UMLAL
                uint64_t old = ((uint64_t)R[Rd] << 32) | R[Ra];
                uint64_t r = old + (uint64_t)R[Rn] * R[Rm];
                R[Ra] = (uint32_t)r;
                R[Rd] = (uint32_t)(r >> 32);
                return;
            }
            goto unimpl32;
        }

        // ── Wide conditional branch T3 (1111 0 S cond imm6 | 10 J1 0 J2 imm11) ──
        // and unconditional T4
        if ((hw >> 24) == 0xF0 || (hw >> 24) == 0xF1 ||
            (hw >> 24) == 0xF2 || (hw >> 24) == 0xF3 ||
            (hw >> 24) == 0xF4 || (hw >> 24) == 0xF5 ||
            (hw >> 24) == 0xF6 || (hw >> 24) == 0xF7) {
            uint8_t S    = (hw >> 26) & 1;
            uint8_t cond = (hw >> 22) & 0xF;
            uint32_t imm6= (hw >> 16) & 0x3F;
            uint32_t J1  = (hw >> 13) & 1;
            uint32_t J2  = (hw >> 11) & 1;
            uint32_t imm11 = hw & 0x7FF;

            if (cond == 0xF) { // B T4 unconditional wide
                uint32_t I1 = (J1 ^ S) ^ 1;
                uint32_t I2 = (J2 ^ S) ^ 1;
                uint32_t off32 = (S<<24)|(I1<<23)|(I2<<22)|((imm6&0x3FF)<<12)|(imm11<<1);
                if (S) off32 |= 0xFF000000u;
                branch_to(R[PC] + (int32_t)off32);
            } else { // B T3 conditional wide
                int32_t off = (int32_t)(
                    ((uint32_t)S << 20) |
                    (J2 << 19) | (J1 << 18) |
                    (imm6 << 12) | (imm11 << 1));
                if (S) off = (int32_t)(off | 0xFFE00000u); // sign extend 21 bits
                if (cond_check(cond)) branch_to(R[PC] + off);
            }
            return;
        }

        // ── MSR / MRS / CPS / Hints ───────────────────────────────────────
        if ((hw >> 20) == 0xF3A || (hw >> 20) == 0xF38 || (hw >> 20) == 0xF3E) {
            // NOP.W, MRS, MSR, etc. - mostly ignore for now
            uint8_t op_h = (hw >> 20) & 0xFF;
            if (op_h == 0xF3E) { // MRS
                uint8_t Rd   = (hw >> 8) & 0xF;
                uint8_t SYSm = hw & 0xFF;
                switch (SYSm) {
                case 0: R[Rd] = xPSR; break;
                case 16: R[Rd] = xPSR; break; // APSR
                case 17: R[Rd] = xPSR; break; // IAPSR
                case 18: R[Rd] = xPSR; break; // EAPSR
                case 19: R[Rd] = xPSR; break; // xPSR
                case 20: R[Rd] = R[PC]; break; // IPSR
                default: R[Rd] = 0; break;
                }
                return;
            }
            if (op_h == 0xF38) { // MSR
                uint8_t SYSm = hw & 0xFF;
                uint8_t Rn   = (hw >> 16) & 0xF;
                if (SYSm < 4) xPSR = R[Rn]; // APSR group
                return;
            }
            // NOP.W (F3AF 8000) and other hints
            return;
        }
    }

    unimpl32:
    ++unimpl_count;
    std::cout << "[CPU] UNIMPL 32-bit 0x" << std::hex << std::setw(8)
              << std::setfill('0') << hw << " at 0x" << ia << std::endl;
}

// ──────────────────────────────────────────────────────────────────────────────
// Debug dump
// ──────────────────────────────────────────────────────────────────────────────

void CPU::dump() const {
    std::cout << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        std::cout << "R" << std::dec << std::setw(2) << i << ": 0x"
                  << std::hex << std::setw(8) << R[i];
        if (i % 4 == 3) std::cout << "\n";
        else            std::cout << "  ";
    }
    std::cout << "xPSR: 0x" << std::setw(8) << xPSR
              << "  N=" << N() << " Z=" << Z() << " C=" << C() << " V=" << V() << "\n";
}
