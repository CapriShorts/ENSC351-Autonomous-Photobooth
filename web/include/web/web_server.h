// web_server.h
// Web interface for the AEPB photo booth.
// Author: Ambardeep Chahal, AEPB team (ENSC 351, Fall 2026)

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEB_STATE_IDLE = 0,
    WEB_STATE_COUNTDOWN,
    WEB_STATE_CAPTURING,
    WEB_STATE_SAVING,
    WEB_STATE_ERROR,
} WebBoothState;

typedef enum {
    WEB_FLASH_AUTO = 0,
    WEB_FLASH_ON,
    WEB_FLASH_OFF,
} WebFlashMode;

typedef struct {
    WebBoothState state;
    int countdownRemaining;   // seconds left while counting down
    int countdownTotal;       // length of the current countdown
    int lightLevel;           // 0..100, -1 if unknown
    bool flashWillFire;
    char message[128];        // shown when state is WEB_STATE_ERROR
} WebStatus;

typedef struct {
    int countdownSeconds;     // 1..10
    WebFlashMode flashMode;
    uint8_t ringRed;
    uint8_t ringGreen;
    uint8_t ringBlue;
    int ringBrightness;       // 0..100
    bool soundEnabled;
    bool gestureEnabled;
} WebSettings;

// Called from the server's worker threads: must be thread-safe and return
// quickly. Any hook left NULL makes its endpoint return 501.
typedef struct {
    void (*getStatus)(WebStatus *status);
    void (*getSettings)(WebSettings *settings);
    // Return false and write a message into error to reject the settings.
    bool (*applySettings)(const WebSettings *settings, char *error, int errorSize);
    // Same as the capture button. Return false if busy.
    bool (*requestCapture)(void);
    // Same as the cancel button.
    void (*requestCancel)(void);
} WebHooks;

typedef struct {
    const char *photoDir;     // folder the photos are saved in (default "./photos")
    const char *wwwDir;       // website files (default "./www")
    int port;                 // default 8080
    bool allowDelete;         // false hides Delete on the page
} WebConfig;

// Starts the server thread. Returns false if the port or folders can't be used.
bool WebServer_init(const WebConfig *config, const WebHooks *hooks);
void WebServer_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif
