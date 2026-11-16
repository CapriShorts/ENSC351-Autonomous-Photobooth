#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>

#include "hal/light_sensor.h"

#define NEOPIXEL_PADCONF_ADDR  0x0408401CU
#define PAD_OUTPUT_DISABLE    (1U << 21)


int main(void)
{
    printf("Photo Booth - Light Sensor Test\n");

    if (LightSensor_init() != 0) {
        fprintf(stderr, "Failed to initialize light sensor.\n");
        return 1;
    }

    printf("Light sensor initialized successfully.\n");

    const struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = 500000000L
    };

    while (1) {
        double voltage = LightSensor_readVoltage();

        printf("Light sensor: %.3f V\n", voltage);

        nanosleep(&delay, NULL);
    }

    LightSensor_cleanup();

    return 0;
}