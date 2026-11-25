#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>
#include "neopixel.h"

#define GPIO_BASE 0x04201000U
#define GPIO_DIR   (*(volatile uint32_t *)(GPIO_BASE + 0x10))
#define GPIO_INPUT (*(volatile uint32_t *)(GPIO_BASE + 0x20))

#define TRIGGER_MASK (1U << 10)

#define CANCEL_THRESHOLD_CYCLES 80000000U
#define FLASH_THRESHOLD_CYCLES 320000000U

static uint32_t read_cycles(void)
{
    uint32_t value;

    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0"
                     : "=r"(value) :: "memory");

    return value;
}

int main(void)
{
    NeoPixel_init();

    // GPIO24 as input
    GPIO_DIR |= TRIGGER_MASK;

    NeoPixel_clear();

    while (1) {

        // Wait for GPIO24 to go HIGH
        while (!(GPIO_INPUT & TRIGGER_MASK)) {
        }

        // Measure pulse duration
        uint32_t pulseStart = read_cycles();

        while (GPIO_INPUT & TRIGGER_MASK) {
        }

        uint32_t pulseCycles =
            read_cycles() - pulseStart;

        // Ignore cancel commands while idle
        if (pulseCycles < CANCEL_THRESHOLD_CYCLES) {
            continue;
        }

        // Long pulse = flash, short pulse = no flash
        bool flashRequired =
            pulseCycles >= FLASH_THRESHOLD_CYCLES;

        NeoPixel_startCountdown(flashRequired);
    }

    return 0;
}