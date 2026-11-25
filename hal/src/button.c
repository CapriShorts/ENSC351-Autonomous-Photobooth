#include "hal/button.h"

#include <errno.h>
#include <gpiod.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

enum { EVENT_BATCH_SIZE = 16 };
static const uint64_t DEBOUNCE_NS = 25000000;

static const struct {
    const char *name;
    int press;
} button_lines[] = {
    { "GPIO4", BUTTON_START },
    { "GPIO6", BUTTON_STOP },
};

enum { BUTTON_COUNT = sizeof(button_lines) / sizeof(button_lines[0]) };

struct ButtonInput {
    struct gpiod_line *lines[BUTTON_COUNT];
    struct pollfd inputs[BUTTON_COUNT];
    uint64_t last_press_ns[BUTTON_COUNT];
};

ButtonInput *button_open(void)
{
    ButtonInput *buttons = calloc(1, sizeof(*buttons));
    if (!buttons)
        return NULL;

    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        buttons->lines[i] = gpiod_line_find(button_lines[i].name);
        if (!buttons->lines[i])
            goto fail;

        if (gpiod_line_request_falling_edge_events_flags(buttons->lines[i],
                "photobooth-buttons", GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) < 0)
            goto fail;

        int event_fd = gpiod_line_event_get_fd(buttons->lines[i]);
        if (event_fd < 0)
            goto fail;

        buttons->inputs[i] = (struct pollfd){ .fd = event_fd, .events = POLLIN };
    }

    return buttons;

fail: {
        int saved_errno = errno;
        button_close(buttons);
        errno = saved_errno;
        return NULL;
    }
}

int button_wait(ButtonInput *buttons, int timeout_ms)
{
    if (!buttons) {
        errno = EINVAL;
        return -1;
    }

    int ready = poll(buttons->inputs, BUTTON_COUNT, timeout_ms);
    if (ready <= 0)
        return ready;

    int presses = 0;
    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        short events_ready = buttons->inputs[i].revents;
        if (events_ready & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = EIO;
            return -1;
        }
        if (!(events_ready & POLLIN))
            continue;

        struct gpiod_line_event events[EVENT_BATCH_SIZE];
        int count = gpiod_line_event_read_fd_multiple(
            buttons->inputs[i].fd, events, EVENT_BATCH_SIZE);
        if (count < 0)
            return -1;

        for (int event_index = 0; event_index < count; ++event_index) {
            const struct timespec *ts = &events[event_index].ts;
            uint64_t now_ns = (uint64_t)ts->tv_sec * UINT64_C(1000000000)
                            + (uint64_t)ts->tv_nsec;
            uint64_t previous_ns = buttons->last_press_ns[i];
            if (previous_ns && now_ns - previous_ns < DEBOUNCE_NS)
                continue;

            buttons->last_press_ns[i] = now_ns;
            presses |= button_lines[i].press;
        }
    }

    return presses;
}

void button_close(ButtonInput *buttons)
{
    if (!buttons)
        return;

    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        if (buttons->lines[i])
            gpiod_line_close_chip(buttons->lines[i]);
    }
    free(buttons);
}
