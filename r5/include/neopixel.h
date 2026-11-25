
#ifndef NEOPIXEL_H
#define NEOPIXEL_H

#include <stdint.h>
#include <stdbool.h>

#define NEOPIXEL_COUNT 24

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} NeoPixel_Color;

void NeoPixel_init(void);

void NeoPixel_setPixel(uint8_t index,
                       uint8_t red,
                       uint8_t green,
                       uint8_t blue);

void NeoPixel_setAll(uint8_t red,
                     uint8_t green,
                     uint8_t blue);

void NeoPixel_show(void);
void NeoPixel_clear(void);

// Countdown animation with optional flash
void NeoPixel_startCountdown(bool enableFlash);

/* Return false if cancelled before the flash starts. */
bool NeoPixel_startCountdownCancelable(bool enableFlash,
                                      bool (*cancelRequested)(void));

#endif
