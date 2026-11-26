#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <stdbool.h>
#include <gpiod.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>

#include "hal/light_sensor.h"
#include "grabber.h"

#define FLASH_THRESHOLD_V 0.20

#define GPIO_CHIP "gpiochip0"
#define TRIGGER_LINE 10

#define CANCEL_PULSE_MS 50
#define NO_FLASH_PULSE_MS 200
#define FLASH_PULSE_MS 800

#define COUNTDOWN_MS 3000
#define FLASH_DURATION_MS 2000
#define R5_MARGIN_MS 100
#define POLL_INTERVAL_MS 20

static volatile sig_atomic_t running = 1;
static pid_t previewPid = -1;

// Get monotonic time in milliseconds
static int64_t get_time_ms(void)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    return (int64_t)now.tv_sec * 1000
           + now.tv_nsec / 1000000;
}

// Start FFmpeg live camera preview
static void start_preview(void)
{
    if (previewPid > 0) {
        return;
    }

    previewPid = fork();

    if (previewPid < 0) {
        perror("[PREVIEW] Failed to start preview");
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
               "pad=1024:600:(ow-iw)/2:(oh-ih)/2,"
               "settb=1/30,setpts=N",
               "-enc_time_base", "1:30",
               "-fps_mode", "passthrough",
               "-c:v", "rawvideo",
               "-pix_fmt", "bgra",
               "-f", "fbdev",
               "/dev/fb0",
               (char *)NULL);

        perror("[PREVIEW] Failed to execute FFmpeg");
        _exit(1);
    }

    printf("[PREVIEW] Camera preview process started.\n");
}

// Stop FFmpeg and release the camera
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

    printf("[PREVIEW] Camera preview stopped.\n");
}

static void clear_display(void)
{
    FILE *fb = fopen("/dev/fb0", "wb");

    if (fb == NULL) {
        perror("[DISPLAY] Failed to open framebuffer");
        return;
    }

    char zeros[4096] = {0};

    while (fwrite(zeros, 1, sizeof(zeros), fb)
           == sizeof(zeros)) {
        // Continue until framebuffer is filled
    }

    fclose(fb);

    printf("[DISPLAY] Framebuffer cleared.\n");
}

static void capture_photo(void)
{
    int64_t captureStart = get_time_ms();

    printf("[CAPTURE] Photo capture requested.\n");

    printf("[PREVIEW] Pausing live preview...\n");
    stop_preview();

    printf("[CAPTURE] Starting camera capture...\n");
    Grabber_capture();

    printf("[CAPTURE] Waiting for image processing...\n");
    Grabber_cleanup();

    printf("[CAPTURE] Capture operation finished.\n");

    printf("[TIMING] Total capture duration: %lld ms\n",
           (long long)(get_time_ms() - captureStart));

    if (running) {
        printf("[PREVIEW] Restoring live preview...\n");
        start_preview();
    }
}

// Handle application termination
static void handle_signal(int signal)
{
    (void)signal;
    running = 0;
}

// Millisecond delay
static void delay_ms(long milliseconds)
{
    struct timespec delay = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (milliseconds % 1000) * 1000000L
    };

    nanosleep(&delay, NULL);
}

// Send command pulse to R5
static int send_pulse(struct gpiod_line *trigger,
                      long duration_ms)
{
    if (gpiod_line_set_value(trigger, 1) < 0) {
        perror("[GPIO] Failed to set GPIO HIGH");
        return -1;
    }

    delay_ms(duration_ms);

    if (gpiod_line_set_value(trigger, 0) < 0) {
        perror("[GPIO] Failed to set GPIO LOW");
        return -1;
    }

    return 0;
}

int main(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("Autonomous Photo Booth\n");

    // Initialize ambient light sensor
    if (LightSensor_init() != 0) {

        fprintf(stderr,
                "[SENSOR] Failed to initialize light sensor.\n");

        return 1;
    }

    printf("[SENSOR] Light sensor initialized.\n");

    // Open GPIO chip
    struct gpiod_chip *chip =
        gpiod_chip_open_by_name(GPIO_CHIP);

    if (!chip) {

        perror("[GPIO] Failed to open GPIO chip");

        LightSensor_cleanup();

        return 1;
    }

    // Configure R5 trigger GPIO
    struct gpiod_line *trigger =
        gpiod_chip_get_line(chip, TRIGGER_LINE);

    if (!trigger ||
        gpiod_line_request_output(
            trigger, "photobooth", 0) < 0) {

        perror("[GPIO] Failed to request GPIO24");

        gpiod_chip_close(chip);
        LightSensor_cleanup();

        return 1;
    }

    printf("[GPIO] GPIO24 initialized.\n");

    // Start live camera preview
    start_preview();

    // Countdown tracking
    bool countdownActive = false;
    int64_t countdownStart = 0;
    int64_t r5BusyUntil = 0;

    // Monitor terminal input without blocking
    struct pollfd keyboard = {
        .fd = STDIN_FILENO,
        .events = POLLIN
    };

    printf("\nPhoto Booth Controls:\n");
    printf("Enter: Start countdown\n");
    printf("C + Enter: Cancel countdown\n");
    printf("P + Enter: Capture test photo\n");
    printf("Ctrl+C: Exit\n");

    char input[32];

    printf("\n[SYSTEM] Waiting for command...\n");

    while (running) {

        int result = poll(&keyboard, 1, POLL_INTERVAL_MS);

        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            perror("[SYSTEM] Poll failed");
            break;
        }

        if (!running) {
            break;
        }

        // Process keyboard commands
        if (result > 0 && (keyboard.revents & POLLIN)) {

            if (fgets(input, sizeof(input), stdin) == NULL
                || !running) {

                break;
            }

            // Cancel countdown
            if (input[0] == 'c' || input[0] == 'C') {

                if (!countdownActive) {

                    printf("[R5] No active countdown to cancel.\n");

                    continue;
                }

                printf("[R5] Cancelling countdown...\n");

                if (send_pulse(trigger, CANCEL_PULSE_MS) < 0) {
                    break;
                }

                countdownActive = false;
                r5BusyUntil = get_time_ms() + R5_MARGIN_MS;

                printf("[R5] Cancel command sent.\n");
                printf("[CAPTURE] Photo capture cancelled.\n");
                printf("[SYSTEM] Waiting for command...\n");

                continue;
            }

            // Manual photo capture
            if (input[0] == 'p' || input[0] == 'P') {

                if (countdownActive) {

                    printf("[CAPTURE] Countdown already active.\n");

                    continue;
                }

                capture_photo();

                printf("[SYSTEM] Waiting for command...\n");

                continue;
            }

            // Ignore unrecognized commands
            if (input[0] != '\n') {

                printf("[SYSTEM] Unknown command.\n");

                continue;
            }

            // Prevent overlapping countdowns
            if (countdownActive) {
                printf("[R5] Countdown already active.\n");
                continue;
            }

            // Ensure R5 has finished the previous flash
            if (get_time_ms() < r5BusyUntil) {
                printf("[R5] Still completing previous sequence.\n");
                printf("[SYSTEM] Please try again shortly.\n");
                continue;
            }

            // Read ambient light
            double voltage = LightSensor_readVoltage();

            if (voltage < 0.0) {

                fprintf(stderr,
                        "[SENSOR] Failed to read light sensor.\n");

                break;
            }

            // Determine whether flash is required
            bool flashRequired =
                voltage < FLASH_THRESHOLD_V;

            printf("[SENSOR] Ambient light: %.3f V\n",
                   voltage);

            printf("[SENSOR] Flash required: %s\n",
                   flashRequired ? "YES" : "NO");

            // Trigger R5 countdown
            printf("[R5] Sending countdown trigger...\n");

            if (send_pulse(trigger,
                           flashRequired
                               ? FLASH_PULSE_MS
                               : NO_FLASH_PULSE_MS) < 0) {

                break;
            }

            if (!running) {
                break;
            }

            countdownStart = get_time_ms();
            countdownActive = true;

            r5BusyUntil = countdownStart
                          + COUNTDOWN_MS
                          + (flashRequired ? FLASH_DURATION_MS : 0)
                          + R5_MARGIN_MS;

            printf("[R5] Countdown triggered.\n");
            printf("[COUNTDOWN] Three-second countdown started.\n");
        }

        if (keyboard.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            break;
        }

        // Check whether countdown has finished
        if (countdownActive &&
            get_time_ms() - countdownStart >= COUNTDOWN_MS) {

            countdownActive = false;

            printf("[COUNTDOWN] Countdown timer completed.\n");

            capture_photo();

            printf("[SYSTEM] Waiting for command...\n");
        }
    }

    printf("\n[SYSTEM] Shutting down...\n");

    // Cancel any remaining R5 countdown
    send_pulse(trigger, CANCEL_PULSE_MS);

    // Release GPIO resources
    gpiod_line_set_value(trigger, 0);
    gpiod_line_release(trigger);
    gpiod_chip_close(chip);

    // Clean up camera and light sensor
    stop_preview();
    clear_display();
    Grabber_cleanup();
    LightSensor_cleanup();

    printf("[SYSTEM] Photo booth stopped.\n");

    return 0;
}