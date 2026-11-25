#include <stdint.h>
#include <zephyr/kernel.h>
#include "neopixel.h"
#include "../../shared/booth_protocol.h"

#define GPIO_BASE 0x04201000U
#define GPIO_DIR   (*(volatile uint32_t *)(GPIO_BASE + 0x10))
#define GPIO_CLEAR (*(volatile uint32_t *)(GPIO_BASE + 0x1C))
#define GPIO_MASK (1U << 7)

#define GPIO_INPUT (*(volatile uint32_t *)(GPIO_BASE + 0x20))
#define TRIGGER_MASK (1U << 10)
#define PADCONF (*(volatile uint32_t *)0x0408401CU)

static uint32_t read_cycles(void)
{
    uint32_t value;
    __asm__ volatile("mrc p15, 0, %0, c9, c13, 0"
                     : "=r"(value) :: "memory");
    return value;
}

static void enable_counter(void)
{
    uint32_t value;

    __asm__ volatile("mrc p15, 0, %0, c9, c12, 0"
                     : "=r"(value));

    value |= (1U << 0) | (1U << 2);
    value &= ~(1U << 3);

    __asm__ volatile("mcr p15, 0, %0, c9, c12, 0"
                     :: "r"(value) : "memory");

    value = (1U << 31);

    __asm__ volatile("mcr p15, 0, %0, c9, c12, 1"
                     :: "r"(value) : "memory");

    __asm__ volatile("isb" ::: "memory");
}

static bool cancel_requested(void)
{
    return (GPIO_INPUT & TRIGGER_MASK) != 0;
}

int main(void)
{
    // Existing NeoPixel output setup
    PADCONF &= ~(1U << 21);
    GPIO_DIR &= ~GPIO_MASK;
    GPIO_CLEAR = GPIO_MASK;

    // GPIO24 as input
    GPIO_DIR |= TRIGGER_MASK;

    enable_counter();
    NeoPixel_init();

    while (1) {
        while (!(GPIO_INPUT & TRIGGER_MASK)) {
        }

        uint32_t pulseStart = read_cycles();
        bool expired = false;
        while (GPIO_INPUT & TRIGGER_MASK) {
            if ((uint32_t)(read_cycles() - pulseStart) >=
                BOOTH_START_MAX_MS * 800000U)
                expired = true;
        }

        uint32_t pulseCycles = read_cycles() - pulseStart;
        if (expired || pulseCycles < BOOTH_START_MIN_MS * 800000U)
            continue;

        bool flashRequired = pulseCycles < BOOTH_START_THRESHOLD_MS * 800000U;
        NeoPixel_startCountdownCancelable(flashRequired, cancel_requested);

        /* Consume the remainder of a cancel pulse before accepting another start. */
        while (GPIO_INPUT & TRIGGER_MASK) {
        }
    }

    return 0;
}
