#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <gpiod.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#include "hal/light_sensor.h"
#include "grabber.h"

#define FLASH_THRESHOLD_V 0.20

#define GPIO_CHIP "gpiochip0"
#define TRIGGER_LINE 10

#define CANCEL_PULSE_MS 50
#define NO_FLASH_PULSE_MS 200
#define FLASH_PULSE_MS 800

static volatile sig_atomic_t running = 1;
static pid_t previewPid = -1;

static void start_preview(void)
{
    if (previewPid > 0) {
        return;
    }

    previewPid = fork();

    if (previewPid < 0) {
        perror("Failed to start preview");
        return;
    }

    if (previewPid == 0) {

        execlp("ffmpeg", "ffmpeg",
               "-hide_banner",
               "-loglevel", "warning",
               "-nostdin",
               "-f", "video4linux2",
               "-i", "/dev/video3",
               "-vf",
               "scale=1024:600:force_original_aspect_ratio=decrease,"
               "pad=1024:600:(ow-iw)/2:(oh-ih)/2",
               "-fps_mode", "passthrough",
               "-c:v", "rawvideo",
               "-pix_fmt", "bgra",
               "-f", "fbdev",
               "/dev/fb0",
               (char *)NULL);

        perror("Failed to execute FFmpeg");
        _exit(1);
    }

    printf("Camera preview started.\n");
}

static void stop_preview(void)
{
    if (previewPid <= 0) {
        return;
    }

    kill(previewPid, SIGTERM);

    while (waitpid(previewPid, NULL, 0) < 0) {
        if (errno != EINTR) {
            break;
        }
    }

    previewPid = -1;

    printf("Camera preview stopped.\n");
}

static void capture_photo(void)
{
    stop_preview();

    printf("Capturing photo...\n");

    Grabber_capture();

    // Wait for capture to finish and release the camera
    Grabber_cleanup();

    // Restore live preview
    if (running) {
        start_preview();
    }
}

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

static int send_pulse(struct gpiod_line *trigger,
                      long duration_ms)
{
    if (gpiod_line_set_value(trigger, 1) < 0) {
        perror("Failed to set GPIO HIGH");
        return -1;
    }

    delay_ms(duration_ms);

    if (gpiod_line_set_value(trigger, 0) < 0) {
        perror("Failed to set GPIO LOW");
        return -1;
    }

    return 0;
}

int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    printf("Autonomous Photo Booth\n");

    // Initialize ambient light sensor
    if (LightSensor_init() != 0) {

        fprintf(stderr,
                "Failed to initialize light sensor.\n");

        return 1;
    }

    // Open GPIO chip
    struct gpiod_chip *chip =
        gpiod_chip_open_by_name(GPIO_CHIP);

    if (!chip) {

        perror("Failed to open GPIO chip");

        LightSensor_cleanup();

        return 1;
    }

    // Configure R5 trigger GPIO
    struct gpiod_line *trigger =
        gpiod_chip_get_line(chip, TRIGGER_LINE);

    if (!trigger ||
        gpiod_line_request_output(
            trigger, "photobooth", 0) < 0) {

        perror("Failed to request GPIO24");

        gpiod_chip_close(chip);
        LightSensor_cleanup();

        return 1;
    }

    printf("GPIO24 initialized.\n");

    // Start live camera preview
    start_preview();

    printf("\nPhoto Booth Controls:\n");
    printf("Enter: Start countdown\n");
    printf("C + Enter: Cancel countdown\n");
    printf("P + Enter: Capture test photo\n");
    printf("Ctrl+C: Exit\n");

    char input[32];

    while (running) {

        printf("\nWaiting for command...\n");

        if (fgets(input, sizeof(input), stdin) == NULL
            || !running) {

            break;
        }

        // Cancel countdown
        if (input[0] == 'c' || input[0] == 'C') {

            printf("Cancelling countdown...\n");

            if (send_pulse(trigger,
                           CANCEL_PULSE_MS) < 0) {

                break;
            }

            printf("Cancel command sent.\n");

            continue;
        }

        // Manual photo capture
        // Manual photo capture
        if (input[0] == 'p' || input[0] == 'P') {

            capture_photo();

            continue;
        }

        // Ignore unrecognized commands
        if (input[0] != '\n') {

            printf("Unknown command.\n");

            continue;
        }

        // Read ambient light
        double voltage = LightSensor_readVoltage();

        if (voltage < 0.0) {

            fprintf(stderr,
                    "Failed to read light sensor.\n");

            break;
        }

        // Determine whether flash is required
        bool flashRequired =
            voltage < FLASH_THRESHOLD_V;

        printf("Light: %.3f V | Flash: %s\n",
               voltage,
               flashRequired ? "YES" : "NO");

        // Trigger R5 countdown
        printf("Starting countdown...\n");

        if (send_pulse(trigger,
                       flashRequired
                           ? FLASH_PULSE_MS
                           : NO_FLASH_PULSE_MS) < 0) {

            break;
        }

        printf("Countdown triggered.\n");
    }

    // Cancel any remaining R5 countdown
    send_pulse(trigger, CANCEL_PULSE_MS);

    // Release GPIO resources
    gpiod_line_set_value(trigger, 0);
    gpiod_line_release(trigger);
    gpiod_chip_close(chip);

    // Clean up camera and light sensor
    stop_preview();
    Grabber_cleanup();
    LightSensor_cleanup();

    printf("Photo booth stopped.\n");

    return 0;
}