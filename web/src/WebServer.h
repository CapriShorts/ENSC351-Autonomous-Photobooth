// WebServer.h
// Web interface for the AEPB photo booth.
// Author: Ambardeep Chahal, AEPB team (ENSC 351, Fall 2026)
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace httplib {
class Server;
}

namespace web {

// Settings that can be changed from the website.
struct BoothSettings {
    int countdownSeconds = 3;          // 1..10
    std::string flashMode = "auto";    // "auto" (use light sensor) | "on" | "off"
    std::string ringColor = "#ffffff"; // ring light colour, "#rrggbb"
    int ringBrightness = 60;           // 0..100 %
    bool soundEnabled = true;          // audio countdown beeps
    bool gestureEnabled = false;       // gesture trigger (stretch goal)
};

// Current booth state.
struct BoothStatus {
    // One of: "IDLE", "COUNTDOWN", "CAPTURING", "SAVING", "ERROR"
    std::string state = "IDLE";
    int countdownRemaining = 0; // seconds left while state == COUNTDOWN
    int countdownTotal = 0;     // length of the current countdown
    int lightLevel = -1;        // 0..100 from the photoresistor, -1 if unknown
    bool flashWillFire = false; // true if the next capture will use the flash
    std::string message;        // optional detail, e.g. an error description
};

// Callbacks into the main app. Called from the server's worker threads, so
// they must be thread-safe. An empty hook makes its endpoint return 501.
struct BoothHooks {
    std::function<BoothStatus()> getStatus;
    std::function<BoothSettings()> getSettings;

    // Returns false and sets error to reject the settings.
    std::function<bool(const BoothSettings &, std::string &error)> applySettings;

    // Same as the capture button. Returns false if busy.
    std::function<bool()> requestCapture;

    // Same as the cancel button.
    std::function<void()> requestCancel;
};

struct WebConfig {
    std::string host = "0.0.0.0"; // listen on every interface (USB + Wi-Fi)
    int port = 8080;
    std::string wwwDir = "./www";     // website files
    std::string photoDir = "./photos"; // where the storage thread saves photos
    bool allowDelete = true;           // let visitors delete photos
    int workerThreads = 4;             // HTTP worker threads
};

class WebServer {
public:
    WebServer(WebConfig config, BoothHooks hooks);
    ~WebServer(); // calls stop()

    WebServer(const WebServer &) = delete;
    WebServer &operator=(const WebServer &) = delete;

    // Binds the port and starts the server thread. Returns false on failure.
    bool start();

    // Stops the server and joins its thread. Safe to call more than once.
    void stop();

    bool isRunning() const;
    int port() const { return config_.port; }

private:
    void registerRoutes();

    WebConfig config_;
    BoothHooks hooks_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    long long startTimeMs_ = 0;
};

} // namespace web
