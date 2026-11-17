
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>

#include "hal/light_sensor.h"

// Initial threshold; adjust after testing.
#define FLASH_THRESHOLD_V 0.20

static volatile sig_atomic_t running = 1;

static void handle_signal(int signal)
{
    (void)signal;
    running = 0;
}

int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Photo Booth - Automatic Flash Detection\n");

    if (LightSensor_init() != 0) {
        fprintf(stderr, "Failed to initialize light sensor.\n");
        return 1;
    }

    printf("Light sensor initialized successfully.\n");

    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 500000000L
    };

    while (running) {
        double voltage = LightSensor_readVoltage();

        if (voltage < 0.0) {
            fprintf(stderr, "Failed to read light sensor.\n");
            break;
        }

        bool flashRequired = voltage < FLASH_THRESHOLD_V;

        printf("Light: %.3f V | Flash: %s\n",
               voltage,
               flashRequired ? "YES" : "NO");

        nanosleep(&delay, NULL);
    }

    LightSensor_cleanup();

    printf("Light sensor stopped.\n");
    return 0;
}
