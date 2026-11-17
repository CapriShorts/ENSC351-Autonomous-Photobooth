// web_demo.c
// Simulated booth for testing the website without the hardware.
// Author: Ambardeep Chahal, AEPB team (ENSC 351, Fall 2026)

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include "web/web_server.h"

#define MAX_SAMPLES 32
#define PATH_LEN 512

static volatile sig_atomic_t running = 1;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static WebStatus status = { .state = WEB_STATE_IDLE, .lightLevel = -1 };
static WebSettings settings = {
    .countdownSeconds = 3,
    .flashMode = WEB_FLASH_AUTO,
    .ringRed = 255, .ringGreen = 255, .ringBlue = 255,
    .ringBrightness = 60,
    .soundEnabled = true,
    .gestureEnabled = false,
};
static bool captureRequested = false;
static bool cancelRequested = false;

static const char *photoDir = "./photos";
static char samples[MAX_SAMPLES][PATH_LEN];
static int sampleCount = 0;
static int nextSample = 0;

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

static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---- Web hooks ----

static void hook_getStatus(WebStatus *out)
{
    pthread_mutex_lock(&lock);
    *out = status;
    pthread_mutex_unlock(&lock);
}

static void hook_getSettings(WebSettings *out)
{
    pthread_mutex_lock(&lock);
    *out = settings;
    pthread_mutex_unlock(&lock);
}

static bool hook_applySettings(const WebSettings *in, char *error, int errorSize)
{
    (void)error;
    (void)errorSize;
    pthread_mutex_lock(&lock);
    settings = *in;
    pthread_mutex_unlock(&lock);
    printf("[booth] Settings: countdown=%ds ring=#%02x%02x%02x @%d%%\n",
           in->countdownSeconds, in->ringRed, in->ringGreen, in->ringBlue, in->ringBrightness);
    return true;
}

static bool hook_requestCapture(void)
{
    bool accepted = false;
    pthread_mutex_lock(&lock);
    if (status.state == WEB_STATE_IDLE && !captureRequested) {
        captureRequested = true;
        accepted = true;
    }
    pthread_mutex_unlock(&lock);
    return accepted;
}

static void hook_requestCancel(void)
{
    pthread_mutex_lock(&lock);
    cancelRequested = true;
    pthread_mutex_unlock(&lock);
}

// ---- Simulated booth ----

// Light level slowly rises and falls between 10 and 90.
static void update_light(void)
{
    int t = (int)((now_ms() / 100) % 160);
    pthread_mutex_lock(&lock);
    status.lightLevel = t < 80 ? 10 + t : 170 - t;
    status.flashWillFire = settings.flashMode == WEB_FLASH_ON ||
                           (settings.flashMode == WEB_FLASH_AUTO && status.lightLevel < 30);
    pthread_mutex_unlock(&lock);
}

static void set_state(WebBoothState state, int remaining, int total, const char *message)
{
    pthread_mutex_lock(&lock);
    status.state = state;
    status.countdownRemaining = remaining;
    status.countdownTotal = total;
    snprintf(status.message, sizeof(status.message), "%s", message ? message : "");
    pthread_mutex_unlock(&lock);
}

// Returns false if cancelled.
static bool wait_or_cancel(long milliseconds)
{
    for (long waited = 0; waited < milliseconds; waited += 50) {
        update_light();
        pthread_mutex_lock(&lock);
        bool cancel = cancelRequested || !running;
        cancelRequested = false;
        pthread_mutex_unlock(&lock);
        if (cancel) {
            return false;
        }
        delay_ms(50);
    }
    return true;
}

static bool copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) {
        return false;
    }
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    char buf[8192];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        ok = false;
    }
    return ok;
}

// Write to a .tmp file, then rename so the photo appears atomically.
static bool save_photo(void)
{
    if (sampleCount == 0) {
        return false;
    }
    const char *src = samples[nextSample++ % sampleCount];

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char name[64];
    snprintf(name, sizeof(name), "photo_%04d%02d%02d_%02d%02d%02d_%03ld.jpg",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);

    char finalPath[PATH_LEN + 64];
    char tmpPath[PATH_LEN + 72];
    snprintf(finalPath, sizeof(finalPath), "%s/%s", photoDir, name);
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", finalPath);

    if (!copy_file(src, tmpPath) || rename(tmpPath, finalPath) != 0) {
        remove(tmpPath);
        return false;
    }
    printf("[booth] Saved %s\n", name);
    return true;
}

static void do_capture(void)
{
    pthread_mutex_lock(&lock);
    int total = settings.countdownSeconds;
    pthread_mutex_unlock(&lock);

    printf("[booth] Countdown %d s\n", total);
    for (int remaining = total; remaining > 0; remaining--) {
        set_state(WEB_STATE_COUNTDOWN, remaining, total, NULL);
        if (!wait_or_cancel(1000)) {
            printf("[booth] Cancelled\n");
            set_state(WEB_STATE_IDLE, 0, 0, NULL);
            return;
        }
    }

    set_state(WEB_STATE_CAPTURING, 0, 0, NULL);
    delay_ms(400);

    set_state(WEB_STATE_SAVING, 0, 0, NULL);
    if (save_photo()) {
        delay_ms(300);
    } else {
        fprintf(stderr, "[booth] Couldn't save a photo. Check --samples.\n");
        set_state(WEB_STATE_ERROR, 0, 0, "Couldn't save the photo.");
        delay_ms(3000);
    }
    set_state(WEB_STATE_IDLE, 0, 0, NULL);
}

static int compare_paths(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void find_samples(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && sampleCount < MAX_SAMPLES) {
        size_t len = strlen(entry->d_name);
        if (len > 4 && strcmp(entry->d_name + len - 4, ".jpg") == 0) {
            snprintf(samples[sampleCount++], PATH_LEN, "%s/%s", dir, entry->d_name);
        }
    }
    closedir(d);
    qsort(samples, (size_t)sampleCount, PATH_LEN, compare_paths);
}

int main(int argc, char **argv)
{
    WebConfig config = { .photoDir = photoDir, .wwwDir = "./www", .port = 8080, .allowDelete = true };
    const char *samplesDir = "./samples";

    for (int i = 1; i < argc; i++) {
        bool hasValue = i + 1 < argc;
        if (strcmp(argv[i], "--port") == 0 && hasValue) {
            config.port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--www") == 0 && hasValue) {
            config.wwwDir = argv[++i];
        } else if (strcmp(argv[i], "--photos") == 0 && hasValue) {
            photoDir = argv[++i];
            config.photoDir = photoDir;
        } else if (strcmp(argv[i], "--samples") == 0 && hasValue) {
            samplesDir = argv[++i];
        } else if (strcmp(argv[i], "--no-delete") == 0) {
            config.allowDelete = false;
        } else {
            fprintf(stderr, "Usage: %s [--port N] [--www DIR] [--photos DIR] "
                            "[--samples DIR] [--no-delete]\n", argv[0]);
            return 1;
        }
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    find_samples(samplesDir);

    WebHooks hooks = {
        .getStatus = hook_getStatus,
        .getSettings = hook_getSettings,
        .applySettings = hook_applySettings,
        .requestCapture = hook_requestCapture,
        .requestCancel = hook_requestCancel,
    };
    if (!WebServer_init(&config, &hooks)) {
        return 1;
    }
    printf("Open http://localhost:%d in a browser. Ctrl+C to quit.\n", config.port);

    while (running) {
        update_light();
        pthread_mutex_lock(&lock);
        bool start = captureRequested;
        captureRequested = false;
        cancelRequested = false;
        pthread_mutex_unlock(&lock);
        if (start) {
            do_capture();
        }
        delay_ms(50);
    }

    printf("\nShutting down...\n");
    WebServer_cleanup();
    return 0;
}
