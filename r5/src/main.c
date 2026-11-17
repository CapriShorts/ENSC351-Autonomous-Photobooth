#include <stdint.h>
#include <zephyr/kernel.h>
#include "neopixel.h"

#define GPIO_BASE 0x04201000U

#define GPIO_DIR   (*(volatile uint32_t *)(GPIO_BASE + 0x10))
#define GPIO_OUT   (*(volatile uint32_t *)(GPIO_BASE + 0x14))
#define GPIO_SET   (*(volatile uint32_t *)(GPIO_BASE + 0x18))
#define GPIO_CLEAR (*(volatile uint32_t *)(GPIO_BASE + 0x1C))

#define GPIO_MASK (1U << 7)

#define PADCONF (*(volatile uint32_t *)0x0408401CU)

static uint32_t read_cycles(void)
{
    uint32_t value;

    __asm__ volatile(
        "mrc p15, 0, %0, c9, c13, 0"
        : "=r"(value)
        :
        : "memory"
    );

    return value;
}

static void enable_counter(void)
{
    uint32_t value;

    __asm__ volatile(
        "mrc p15, 0, %0, c9, c12, 0"
        : "=r"(value)
    );

    value |= (1U << 0) | (1U << 2);
    value &= ~(1U << 3);

    __asm__ volatile(
        "mcr p15, 0, %0, c9, c12, 0"
        :: "r"(value)
        : "memory"
    );

    value = (1U << 31);

    __asm__ volatile(
        "mcr p15, 0, %0, c9, c12, 1"
        :: "r"(value)
        : "memory"
    );

    __asm__ volatile("isb" ::: "memory");
}

static void delay_cycles(uint32_t count)
{
    uint32_t start = read_cycles();

    while ((uint32_t)(read_cycles() - start) < count) {
    }
}


int main(void)
{
    PADCONF &= ~(1U << 21);
    GPIO_DIR &= ~GPIO_MASK;
    GPIO_CLEAR = GPIO_MASK;

    enable_counter();
    NeoPixel_init();

    while (1) {
        NeoPixel_startCountdown(true);

        // Pause between countdowns without k_msleep().
        delay_cycles(800000000U);
    }

    return 0;
}