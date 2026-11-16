#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <stdint.h>

#define NEOPIXEL_COUNT 24

void NeoPixel_init(void);
void NeoPixel_setAll(uint8_t red, uint8_t green, uint8_t blue);
void NeoPixel_clear(void);

#endif