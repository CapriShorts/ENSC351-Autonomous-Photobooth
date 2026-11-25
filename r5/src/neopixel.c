#include "neopixel.h"
#include "../../shared/booth_protocol.h"

#include <stdint.h>
#include <stddef.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#define GPIO_BASE 0x04201000U

#define GPIO_DIR   (*(volatile uint32_t *)(GPIO_BASE + 0x10))
#define GPIO_SET   (*(volatile uint32_t *)(GPIO_BASE + 0x18))
#define GPIO_CLEAR (*(volatile uint32_t *)(GPIO_BASE + 0x1C))

#define GPIO_MASK (1U << 7)

#define T0H_CYCLES 280
#define T1H_CYCLES 560
#define BIT_CYCLES 1000

#define NEOPIXEL_PADCONF_ADDR 0x0408401CU
#define PAD_OUTPUT_DISABLE   (1U << 21)

#define RESET_CYCLES 400000U

#define COUNTDOWN_BRIGHTNESS 10
#define FLASH_BRIGHTNESS 30
#define FLASH_DURATION_MS BOOTH_FLASH_MS

static NeoPixel_Color pixels[NEOPIXEL_COUNT];

static void pinmux_init(void)
{
    volatile uint32_t *padconf =
        (volatile uint32_t *)NEOPIXEL_PADCONF_ADDR;

    *padconf &= ~PAD_OUTPUT_DISABLE;

    __asm__ volatile("dsb sy" ::: "memory");
}

static void cycle_counter_init(void)
{
    uint32_t value;

    __asm__ volatile(
        "mrc p15, 0, %0, c9, c12, 0"
        : "=r"(value)
    );

    value |= (1U << 0);
    value |= (1U << 2);
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

static inline uint32_t cycles(void)
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

//static inline void wait_cycles(uint32_t start,
//                               uint32_t duration)
//{
//    while ((uint32_t)(cycles() - start) < duration) {
//        __asm__ volatile("nop");
//    }
//}

static inline void wait_cycles(uint32_t start, uint32_t duration)
{
    uint32_t previous = start;
    uint32_t unchanged = 0;

    while ((uint32_t)(cycles() - start) < duration) {
        uint32_t current = cycles();

        if (current == previous) {
            if (++unchanged >= 1000000U) {
                break;
            }
        } else {
            previous = current;
            unchanged = 0;
        }
    }
}
static void delay_ms(uint32_t milliseconds)
{
    // Approximate 800 MHz R5 cycle counter.
    // Use chunks to avoid overflowing the 32-bit counter.
    const uint32_t cycles_per_ms = 800000U;

    while (milliseconds > 0) {
        uint32_t chunk_ms = milliseconds > 1000U
                            ? 1000U : milliseconds;

        wait_cycles(cycles(), chunk_ms * cycles_per_ms);
        milliseconds -= chunk_ms;
    }
}

static inline void send_bit(uint8_t bit)
{
    uint32_t high_cycles =
        bit ? T1H_CYCLES : T0H_CYCLES;

    GPIO_SET = GPIO_MASK;

    uint32_t start = cycles();

    wait_cycles(start, high_cycles);

    GPIO_CLEAR = GPIO_MASK;

    wait_cycles(start, BIT_CYCLES);
}

static void send_byte(uint8_t value)
{
    for (int bit = 7; bit >= 0; bit--) {
        send_bit((value >> bit) & 1);
    }
}

void NeoPixel_init(void)
{
    pinmux_init();

    GPIO_CLEAR = GPIO_MASK;
    GPIO_DIR &= ~GPIO_MASK;

    cycle_counter_init();

    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        pixels[i] = (NeoPixel_Color){0, 0, 0};
    }

    GPIO_CLEAR = GPIO_MASK;
    wait_cycles(cycles(), RESET_CYCLES);
}

void NeoPixel_setPixel(uint8_t index,
                       uint8_t red,
                       uint8_t green,
                       uint8_t blue)
{
    if (index >= NEOPIXEL_COUNT) {
        return;
    }

    pixels[index].red = red;
    pixels[index].green = green;
    pixels[index].blue = blue;
}

void NeoPixel_setAll(uint8_t red,
                     uint8_t green,
                     uint8_t blue)
{
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        NeoPixel_setPixel(i, red, green, blue);
    }
}

void NeoPixel_show(void)
{
    unsigned int key = irq_lock();

    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        send_byte(pixels[i].green);
        send_byte(pixels[i].red);
        send_byte(pixels[i].blue);
    }

    GPIO_CLEAR = GPIO_MASK;

    irq_unlock(key);

    wait_cycles(cycles(), 400000);
}

void NeoPixel_clear(void)
{
    NeoPixel_setAll(0, 0, 0);
    NeoPixel_show();
}


static bool countdown_delay(bool (*cancelRequested)(void))
{
    for (unsigned int waited = 0; waited < BOOTH_COUNTDOWN_MS / 3; waited += 5) {
        if (cancelRequested && cancelRequested())
            return false;
        delay_ms(5);
    }
    return !(cancelRequested && cancelRequested());
}

void NeoPixel_startCountdown(bool enableFlash)
{
    (void)NeoPixel_startCountdownCancelable(enableFlash, NULL);
}

bool NeoPixel_startCountdownCancelable(bool enableFlash,
                                      bool (*cancelRequested)(void))
{
    if (cancelRequested && cancelRequested())
        goto cancelled;
    // 3 seconds: Green, all 24 LEDs
    NeoPixel_setAll(0, COUNTDOWN_BRIGHTNESS, 0);
    NeoPixel_show();
    if (!countdown_delay(cancelRequested))
        goto cancelled;

    // 2 seconds: Yellow, 16 LEDs
    NeoPixel_setAll(0, 0, 0);

    for (int i = 0; i < 16; i++) {
        NeoPixel_setPixel(i,
                         COUNTDOWN_BRIGHTNESS,
                         COUNTDOWN_BRIGHTNESS,
                         0);
    }

    NeoPixel_show();
    if (!countdown_delay(cancelRequested))
        goto cancelled;

    // 1 second: Red, 8 LEDs
    NeoPixel_setAll(0, 0, 0);

    for (int i = 0; i < 8; i++) {
        NeoPixel_setPixel(i, COUNTDOWN_BRIGHTNESS, 0, 0);
    }

    NeoPixel_show();
    if (!countdown_delay(cancelRequested))
        goto cancelled;

    // Optional white flash
    if (enableFlash) {
        NeoPixel_setAll(FLASH_BRIGHTNESS,
                        FLASH_BRIGHTNESS,
                        FLASH_BRIGHTNESS);

        NeoPixel_show();
        delay_ms(FLASH_DURATION_MS);
    }

    NeoPixel_clear();
    return true;

cancelled:
    NeoPixel_clear();
    return false;
}
