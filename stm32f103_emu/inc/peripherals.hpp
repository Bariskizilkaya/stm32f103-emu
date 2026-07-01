#pragma once
#include <cstdint>
#include <cstddef>
#include <map>
#include <string>

// GPIO base addresses
static constexpr uint32_t GPIOA_BASE  = 0x40010800u;
static constexpr uint32_t GPIOB_BASE  = 0x40010C00u;
static constexpr uint32_t GPIOC_BASE  = 0x40011000u;
static constexpr uint32_t GPIOD_BASE  = 0x40011400u;

// RCC base
static constexpr uint32_t RCC_BASE    = 0x40021000u;

// SysTick base
static constexpr uint32_t STK_BASE    = 0xE000E000u;

// SCB base
static constexpr uint32_t SCB_BASE    = 0xE000ED00u;

// NVIC base
static constexpr uint32_t NVIC_BASE   = 0xE000E100u;

// Flash interface
static constexpr uint32_t FLASH_REG   = 0x40022000u;

// AFIO
static constexpr uint32_t AFIO_BASE   = 0x40010000u;

// USART bases
static constexpr uint32_t USART1_BASE = 0x40013800u;
static constexpr uint32_t USART2_BASE = 0x40004400u;
static constexpr uint32_t USART3_BASE = 0x40004800u;

class Peripherals {
public:
    std::map<uint32_t, uint32_t> regs;

    // SysTick state
    uint32_t stk_rvr = 0;
    uint32_t stk_cvr = 0;
    uint32_t stk_csr = 0;
    uint64_t ticks   = 0;

    // GPIO ODR tracking for display
    uint32_t gpio_odr[4] = {};

    // Fuzz input stream: peripheral reads consume bytes from here
    const uint8_t* fuzz_data   = nullptr;
    size_t         fuzz_size   = 0;
    size_t         fuzz_cursor = 0;

    // Set to false to suppress all console output (used during fuzzing)
    bool verbose = true;

    uint32_t read(uint32_t addr);
    void     write(uint32_t addr, uint32_t val, int bytes);

private:
    uint32_t rcc_read(uint32_t off);
    void     rcc_write(uint32_t off, uint32_t val);
    void     gpio_write(uint32_t base, uint32_t off, uint32_t val);
    uint32_t gpio_read(uint32_t base, uint32_t off);
    uint32_t usart_read(uint32_t base, uint32_t off);
};
