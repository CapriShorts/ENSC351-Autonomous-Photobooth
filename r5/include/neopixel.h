#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <stdint.h>

#define NEOPIXEL_COUNT 24

int NeoPixel_init(void);

void NeoPixel_setAll(uint8_t red,
                     uint8_t green,
                     uint8_t blue);

void NeoPixel_off(void);

void NeoPixel_cleanup(void);

#endif