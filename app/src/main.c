#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
//#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <gpiod.h>

#include "hal/light_sensor.h"

#define FLASH_THRESHOLD_V 0.20

#define GPIO_CHIP "gpiochip0"
#define TRIGGER_LINE 10

static volatile sig_atomic_t running = 1;

static void handle_signal(int signal)
{
    (void)signal;
    running = 0;
}

static void delay_ms(long milliseconds)
{
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L
    };

    nanosleep(&delay, NULL);
}

int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Photo Booth - GPIO Trigger Test\n");

    if (LightSensor_init() != 0) {
        fprintf(stderr, "Failed to initialize light sensor.\n");
        return 1;
    }

    struct gpiod_chip *chip = gpiod_chip_open_by_name(GPIO_CHIP);

    if (!chip) {
        perror("Failed to open GPIO chip");
        LightSensor_cleanup();
        return 1;
    }

    struct gpiod_line *trigger = gpiod_chip_get_line(chip, TRIGGER_LINE);

    if (!trigger || gpiod_line_request_output(trigger, "photobooth", 0) < 0) {
        perror("Failed to request GPIO24");
        gpiod_chip_close(chip);
        LightSensor_cleanup();
        return 1;
    }

    printf("GPIO24 initialized.\n");
    printf("Press Enter to trigger the countdown, or Ctrl+C to exit.\n");

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

        // Wait for user to press Enter
        int input = getchar();

        if (input == EOF || !running) {
            break;
        }

        // Trigger R5
        printf("Starting countdown...\n");

        if (gpiod_line_set_value(trigger, 1) < 0) {
            perror("Failed to set GPIO HIGH");
            break;
        }

        // Short pulse = no flash; long pulse = flash
        delay_ms(flashRequired ? 200 : 800);

        if (gpiod_line_set_value(trigger, 0) < 0) {
            perror("Failed to set GPIO LOW");
            break;
        }

        printf("Countdown triggered.\n");
    }

    gpiod_line_set_value(trigger, 0);
    gpiod_line_release(trigger);
    gpiod_chip_close(chip);

    LightSensor_cleanup();

    printf("Photo booth stopped.\n");
    return 0;
}