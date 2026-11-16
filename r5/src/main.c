#include <stdint.h>
#include <zephyr/irq.h>

#define NUM_LEDS 24
#define BRIGHTNESS 10

#define GPIO_BASE 0x04201000

#define GPIO_DIR   (*(volatile uint32_t *)(GPIO_BASE + 0x10))
#define GPIO_SET   (*(volatile uint32_t *)(GPIO_BASE + 0x18))
#define GPIO_CLEAR (*(volatile uint32_t *)(GPIO_BASE + 0x1C))

#define GPIO_MASK (1U << 7)

#define T0H_CYCLES 280
#define T1H_CYCLES 560
#define BIT_CYCLES 1000

static void cycle_counter_init(void)
{
    uint32_t value;

    // Enable PMU and reset cycle counter.
    __asm__ volatile(
        "mrc p15, 0, %0, c9, c12, 0"
        : "=r"(value)
    );

    value |= (1U << 0);
    value |= (1U << 2);

    // Count every CPU cycle, not every 64 cycles.
    value &= ~(1U << 3);

    __asm__ volatile(
        "mcr p15, 0, %0, c9, c12, 0"
        :: "r"(value)
        : "memory"
    );

    // Enable cycle counter.
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

static inline void wait_cycles(uint32_t start,
                               uint32_t duration)
{
    while ((uint32_t)(cycles() - start) < duration) {
        __asm__ volatile("nop");
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

static void set_all(uint8_t red,
                    uint8_t green,
                    uint8_t blue)
{
    unsigned int key = irq_lock();

    for (int i = 0; i < NUM_LEDS; i++) {
        send_byte(green);
        send_byte(red);
        send_byte(blue);
    }

    GPIO_CLEAR = GPIO_MASK;

    irq_unlock(key);

    // Reset/latch interval: approximately 500 us.
    wait_cycles(cycles(), 400000);
}

static void delay_long(void)
{
    for (volatile uint32_t i = 0; i < 100000000; i++) {
        __asm__ volatile("nop");
    }
}

int main(void)
{
    GPIO_CLEAR = GPIO_MASK;
    GPIO_DIR &= ~GPIO_MASK;

    cycle_counter_init();

    // Allow the ring to settle.
    delay_long();

    while (1) {
        set_all(BRIGHTNESS, 0, 0);
        delay_long();

        set_all(0, BRIGHTNESS, 0);
        delay_long();

        set_all(0, 0, BRIGHTNESS);
        delay_long();

        set_all(0, 0, 0);
        delay_long();
    }

    return 0;
}
