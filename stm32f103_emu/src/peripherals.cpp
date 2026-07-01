#include "peripherals.hpp"
#include <iostream>
#include <iomanip>

static const char* gpio_name(uint32_t base) {
    switch (base) {
        case GPIOA_BASE: return "GPIOA";
        case GPIOB_BASE: return "GPIOB";
        case GPIOC_BASE: return "GPIOC";
        case GPIOD_BASE: return "GPIOD";
        default:         return "GPIO?";
    }
}

static int gpio_idx(uint32_t base) {
    switch (base) {
        case GPIOA_BASE: return 0;
        case GPIOB_BASE: return 1;
        case GPIOC_BASE: return 2;
        case GPIOD_BASE: return 3;
        default:         return -1;
    }
}

void Peripherals::gpio_write(uint32_t base, uint32_t off, uint32_t val) {
    regs[base + off] = val;
    if (off == 0x0C) { // ODR
        int idx = gpio_idx(base);
        if (idx >= 0) {
            uint32_t old = gpio_odr[idx];
            gpio_odr[idx] = val;
            if (verbose && old != val) {
                std::cout << "[GPIO] " << gpio_name(base) << "_ODR = 0x"
                          << std::hex << std::setw(4) << std::setfill('0') << val;
                for (int i = 0; i < 16; ++i)
                    if (((old >> i) & 1) != ((val >> i) & 1))
                        std::cout << "  PA" << std::dec << i << "=" << ((val >> i) & 1);
                std::cout << std::endl;
            }
        }
    }
    if (off == 0x10) { // BSRR
        int idx = gpio_idx(base);
        if (idx >= 0) {
            uint32_t old  = gpio_odr[idx];
            uint32_t set  = val & 0xFFFF;
            uint32_t clr  = (val >> 16) & 0xFFFF;
            uint32_t nval = (old | set) & ~clr;
            if (nval != old) {
                gpio_odr[idx]     = nval;
                regs[base + 0x0C] = nval;
                if (verbose)
                    std::cout << "[GPIO] " << gpio_name(base) << "_ODR = 0x"
                              << std::hex << std::setw(4) << std::setfill('0') << nval << std::endl;
            }
        }
    }
}

uint32_t Peripherals::gpio_read(uint32_t base, uint32_t off) {
    if (off == 0x08) { // IDR: pull from fuzz stream if available, else mirror ODR
        if (fuzz_data && fuzz_cursor + 1 < fuzz_size) {
            uint32_t lo = fuzz_data[fuzz_cursor++];
            uint32_t hi = fuzz_data[fuzz_cursor++];
            return (hi << 8) | lo;
        }
        if (fuzz_data && fuzz_cursor < fuzz_size)
            return fuzz_data[fuzz_cursor++];
        int idx = gpio_idx(base);
        return (idx >= 0) ? gpio_odr[idx] : 0;
    }
    if (regs.count(base + off)) return regs[base + off];
    return 0;
}

void Peripherals::rcc_write(uint32_t off, uint32_t val) {
    regs[RCC_BASE + off] = val;
}

uint32_t Peripherals::rcc_read(uint32_t off) {
    uint32_t v = regs.count(RCC_BASE + off) ? regs[RCC_BASE + off] : 0;
    if (off == 0x00) v |= 0x00000002; // HSI ready
    if (off == 0x04) v = (v & ~0x0C) | 0x00; // SWS = HSI
    return v;
}

uint32_t Peripherals::usart_read(uint32_t base, uint32_t off) {
    if (off == 0x00) { // SR: TXE+TC always 1, RXNE=1 if fuzz data left
        uint32_t rxne = (fuzz_data && fuzz_cursor < fuzz_size) ? (1u << 5) : 0;
        return 0xC0u | rxne; // TXE(7)+TC(6) set
    }
    if (off == 0x04) { // DR: consume next fuzz byte
        if (fuzz_data && fuzz_cursor < fuzz_size)
            return fuzz_data[fuzz_cursor++];
        return 0;
    }
    return regs.count(base + off) ? regs[base + off] : 0;
}

uint32_t Peripherals::read(uint32_t addr) {
    if (addr >= RCC_BASE && addr < RCC_BASE + 0x400)
        return rcc_read(addr - RCC_BASE);

    for (uint32_t base : {GPIOA_BASE, GPIOB_BASE, GPIOC_BASE, GPIOD_BASE})
        if (addr >= base && addr < base + 0x400)
            return gpio_read(base, addr - base);

    for (uint32_t base : {USART1_BASE, USART2_BASE, USART3_BASE})
        if (addr >= base && addr < base + 0x400)
            return usart_read(base, addr - base);

    if (addr == STK_BASE + 0x10) return stk_csr;
    if (addr == STK_BASE + 0x14) return stk_rvr;
    if (addr == STK_BASE + 0x18) {
        ticks++;
        stk_cvr = (stk_rvr > 0) ? (stk_rvr - (ticks % (stk_rvr + 1))) : 0;
        return stk_cvr;
    }

    if (regs.count(addr)) return regs[addr];
    return 0;
}

void Peripherals::write(uint32_t addr, uint32_t val, int bytes) {
    if (bytes < 4) {
        uint32_t old   = read(addr & ~3u);
        int      shift = (addr & 3) * 8;
        uint32_t mask  = (bytes == 1 ? 0xFFu : 0xFFFFu) << shift;
        val  = (old & ~mask) | ((val << shift) & mask);
        addr = addr & ~3u;
    }

    if (addr >= RCC_BASE && addr < RCC_BASE + 0x400) {
        rcc_write(addr - RCC_BASE, val); return;
    }
    for (uint32_t base : {GPIOA_BASE, GPIOB_BASE, GPIOC_BASE, GPIOD_BASE})
        if (addr >= base && addr < base + 0x400) {
            gpio_write(base, addr - base, val); return;
        }
    for (uint32_t base : {USART1_BASE, USART2_BASE, USART3_BASE})
        if (addr >= base && addr < base + 0x400) {
            regs[addr] = val; return; // TX writes are fire-and-forget
        }

    if (addr == STK_BASE + 0x10) { stk_csr = val; return; }
    if (addr == STK_BASE + 0x14) { stk_rvr = val & 0x00FFFFFF; return; }
    if (addr == STK_BASE + 0x18) { stk_cvr = 0; return; }

    regs[addr] = val;
}
