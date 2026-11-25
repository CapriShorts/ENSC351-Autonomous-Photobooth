#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <gpiod.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "hal/button.h"
#include "hal/light_sensor.h"
#include "../../shared/booth_protocol.h"

#define FLASH_THRESHOLD_V 0.20

typedef enum {
    BOOTH_IDLE,
    BOOTH_START_PULSE,
    BOOTH_COUNTDOWN,
    BOOTH_CANCEL_PULSE,
    BOOTH_SETTLING,
} BoothState;

static volatile sig_atomic_t running = 1;

static void handle_signal(int signal)
{
    (void)signal;
    running = 0;
}

static int64_t now_ms(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) < 0)
        return -1;
    return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static void finish_cancel_pulse(struct gpiod_line *trigger)
{
    struct timespec gap = { .tv_nsec = BOOTH_COMMAND_GAP_MS * 1000000L };
    struct timespec pulse = { .tv_nsec = BOOTH_CANCEL_PULSE_MS * 1000000L };
    /* A falling edge finishes any pending command. The short follow-up pulse
     * cancels an active countdown and is ignored by an idle R5. */
    if (gpiod_line_set_value(trigger, 0) < 0)
        return;
    while (nanosleep(&gap, &gap) < 0 && errno == EINTR) {
    }
    if (gpiod_line_set_value(trigger, 1) < 0)
        return;
    while (nanosleep(&pulse, &pulse) < 0 && errno == EINTR) {
    }
    gpiod_line_set_value(trigger, 0);
}

int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (LightSensor_init() != 0) {
        fprintf(stderr, "Failed to initialize light sensor.\n");
        return EXIT_FAILURE;
    }

    ButtonInput *buttons = button_open();
    struct gpiod_line *trigger = NULL;
    bool trigger_requested = false;
    int result = EXIT_FAILURE;
    if (!buttons) {
        perror("Failed to initialize buttons");
        goto cleanup;
    }

    trigger = gpiod_line_find("GPIO24");
    if (!trigger || gpiod_line_request_output(trigger, "photobooth-trigger", 0) < 0) {
        perror("Failed to request GPIO24");
        goto cleanup;
    }
    trigger_requested = true;

    BoothState state = BOOTH_IDLE;
    bool cancel_requested = false;
    bool flash_required = false;
    int64_t deadline = 0;
    int64_t countdown_start = 0;
    int last_second = -1;

    puts("Photo Booth | Start = GPIO4 | Stop = GPIO6 | Ctrl+C = Exit");
    puts("Ready. Press Start.");

    while (running) {
        int presses = button_wait(buttons, 10);
        if (presses < 0) {
            if (errno == EINTR)
                continue;
            perror("Failed to read buttons");
            goto cleanup;
        }
        if (!running)
            break;

        int64_t now = now_ms();
        if (now < 0) {
            perror("Failed to read clock");
            goto cleanup;
        }

        if (presses & BUTTON_STOP) {
            if (state == BOOTH_START_PULSE || state == BOOTH_COUNTDOWN)
                cancel_requested = true;
        } else if ((presses & BUTTON_START) && state == BOOTH_IDLE) {
            double voltage = LightSensor_readVoltage();
            if (voltage < 0.0) {
                fprintf(stderr, "Failed to read light sensor.\n");
                goto cleanup;
            }
            flash_required = voltage < FLASH_THRESHOLD_V;
            printf("Starting countdown | Light %.3f V | Flash %s\n",
                   voltage, flash_required ? "YES" : "NO");
            if (gpiod_line_set_value(trigger, 1) < 0)
                goto gpio_error;
            now = now_ms();
            if (now < 0) {
                perror("Failed to read clock");
                goto cleanup;
            }
            deadline = now + (flash_required ? BOOTH_START_FLASH_MS
                                            : BOOTH_START_NO_FLASH_MS);
            cancel_requested = false;
            state = BOOTH_START_PULSE;
        }

        if (state == BOOTH_START_PULSE && now >= deadline) {
            if (gpiod_line_set_value(trigger, 0) < 0)
                goto gpio_error;
            countdown_start = now;
            last_second = -1;
            state = BOOTH_COUNTDOWN;
        }

        if (state == BOOTH_COUNTDOWN) {
            int64_t elapsed = now - countdown_start;
            if (cancel_requested && elapsed >= BOOTH_COMMAND_GAP_MS) {
                if (gpiod_line_set_value(trigger, 1) < 0)
                    goto gpio_error;
                deadline = now + BOOTH_CANCEL_PULSE_MS;
                state = BOOTH_CANCEL_PULSE;
                puts("Cancel requested.");
            } else if (!cancel_requested && elapsed < BOOTH_COUNTDOWN_MS) {
                int remaining = (int)((BOOTH_COUNTDOWN_MS - elapsed + 999) / 1000);
                if (remaining != last_second) {
                    printf("Countdown %d\n", remaining);
                    last_second = remaining;
                }
            } else if (elapsed >= BOOTH_COUNTDOWN_MS +
                                  (flash_required ? BOOTH_FLASH_MS : 0) +
                                  BOOTH_FINISH_GUARD_MS) {
                state = BOOTH_IDLE;
                puts("Countdown complete. Ready. Press Start.");
            }
        }

        if (state == BOOTH_CANCEL_PULSE && now >= deadline) {
            if (gpiod_line_set_value(trigger, 0) < 0)
                goto gpio_error;
            /* Allow a flash already in progress to finish before another start. */
            deadline = now + BOOTH_FLASH_MS + BOOTH_COMMAND_GAP_MS;
            state = BOOTH_SETTLING;
        }
        if (state == BOOTH_SETTLING && now >= deadline) {
            state = BOOTH_IDLE;
            puts("Ready. Press Start.");
        }
    }

    result = EXIT_SUCCESS;
    goto cleanup;

gpio_error:
    perror("Failed to update GPIO24");

cleanup:
    if (trigger_requested)
        finish_cancel_pulse(trigger);
    if (trigger)
        gpiod_line_close_chip(trigger);
    button_close(buttons);
    LightSensor_cleanup();
    puts("Photo booth stopped.");
    return result;
}
