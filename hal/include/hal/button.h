#ifndef PHOTOBOOTH_BUTTON_H
#define PHOTOBOOTH_BUTTON_H

typedef struct ButtonInput ButtonInput;

enum ButtonPress {
    BUTTON_START = 1 << 0,
    BUTTON_STOP = 1 << 1,
};

/* Open Start (GPIO4) and Stop (GPIO6). Returns null on error with errno set. */
ButtonInput *button_open(void);

/* Return a bitmask of presses, 0 when no press is accepted, or -1 on error.
 * Use 0 to check without blocking or -1 to wait indefinitely.
 * Call this from one thread per ButtonInput. */
int button_wait(ButtonInput *buttons, int timeout_ms);

/* Release both GPIO lines. Accepts null. */
void button_close(ButtonInput *buttons);

#endif
