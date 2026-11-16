#include <stdint.h>

#include "neopixel.h"

#define BRIGHTNESS 10

static void delay_long(void)
{
    for (volatile uint32_t i = 0; i < 100000000; i++) {
        __asm__ volatile("nop");
    }
}

int main(void)
{
    NeoPixel_init();

    delay_long();

    while (1) {
        NeoPixel_setAll(BRIGHTNESS, 0, 0);
        delay_long();

        NeoPixel_setAll(0, BRIGHTNESS, 0);
        delay_long();

        NeoPixel_setAll(0, 0, BRIGHTNESS);
        delay_long();

        NeoPixel_clear();
        delay_long();
    }

    return 0;
}