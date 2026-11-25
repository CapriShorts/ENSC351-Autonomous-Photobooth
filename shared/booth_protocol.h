#ifndef BOOTH_PROTOCOL_H
#define BOOTH_PROTOCOL_H

/* GPIO24 carries a start pulse while idle and a cancel pulse during countdown.
 * Pulse widths below START_MIN_MS are ignored while the R5 is idle. */
#define BOOTH_START_MIN_MS 100U
#define BOOTH_START_FLASH_MS 200U
#define BOOTH_START_NO_FLASH_MS 800U
#define BOOTH_START_THRESHOLD_MS 400U
#define BOOTH_START_MAX_MS 1200U
#define BOOTH_CANCEL_PULSE_MS 50U
#define BOOTH_COMMAND_GAP_MS 50U
#define BOOTH_COUNTDOWN_MS 3000U
#define BOOTH_FLASH_MS 200U
#define BOOTH_FINISH_GUARD_MS 50U

#endif
