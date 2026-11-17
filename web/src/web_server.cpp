// web_server.cpp
// C interface over the C++ web server.
// Author: Ambardeep Chahal, AEPB team (ENSC 351, Fall 2026)

#include "web/web_server.h"

#include "WebServer.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>

namespace {

std::mutex s_lock;
std::unique_ptr<web::WebServer> s_server;
WebHooks s_hooks{};

const char *stateName(WebBoothState state)
{
    switch (state) {
    case WEB_STATE_IDLE: return "IDLE";
    case WEB_STATE_COUNTDOWN: return "COUNTDOWN";
    case WEB_STATE_CAPTURING: return "CAPTURING";
    case WEB_STATE_SAVING: return "SAVING";
    case WEB_STATE_ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

const char *flashName(WebFlashMode mode)
{
    switch (mode) {
    case WEB_FLASH_ON: return "on";
    case WEB_FLASH_OFF: return "off";
    case WEB_FLASH_AUTO: break;
    }
    return "auto";
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

uint8_t hexByte(const std::string &s, size_t pos)
{
    return static_cast<uint8_t>(hexDigit(s[pos]) * 16 + hexDigit(s[pos + 1]));
}

web::BoothSettings toCpp(const WebSettings &in)
{
    web::BoothSettings out;
    char color[8];
    std::snprintf(color, sizeof(color), "#%02x%02x%02x", in.ringRed, in.ringGreen, in.ringBlue);
    out.countdownSeconds = in.countdownSeconds;
    out.flashMode = flashName(in.flashMode);
    out.ringColor = color;
    out.ringBrightness = in.ringBrightness;
    out.soundEnabled = in.soundEnabled;
    out.gestureEnabled = in.gestureEnabled;
    return out;
}

WebSettings toC(const web::BoothSettings &in)
{
    WebSettings out{};
    out.countdownSeconds = in.countdownSeconds;
    out.flashMode = in.flashMode == "on" ? WEB_FLASH_ON
                  : in.flashMode == "off" ? WEB_FLASH_OFF
                                          : WEB_FLASH_AUTO;
    if (in.ringColor.size() == 7) {
        out.ringRed = hexByte(in.ringColor, 1);
        out.ringGreen = hexByte(in.ringColor, 3);
        out.ringBlue = hexByte(in.ringColor, 5);
    }
    out.ringBrightness = in.ringBrightness;
    out.soundEnabled = in.soundEnabled;
    out.gestureEnabled = in.gestureEnabled;
    return out;
}

web::BoothHooks makeHooks()
{
    web::BoothHooks h;
    if (s_hooks.getStatus) {
        h.getStatus = [] {
            WebStatus c{};
            c.lightLevel = -1;
            s_hooks.getStatus(&c);
            c.message[sizeof(c.message) - 1] = '\0';
            web::BoothStatus s;
            s.state = stateName(c.state);
            s.countdownRemaining = c.countdownRemaining;
            s.countdownTotal = c.countdownTotal;
            s.lightLevel = c.lightLevel;
            s.flashWillFire = c.flashWillFire;
            s.message = c.message;
            return s;
        };
    }
    if (s_hooks.getSettings) {
        h.getSettings = [] {
            WebSettings c{};
            s_hooks.getSettings(&c);
            return toCpp(c);
        };
    }
    if (s_hooks.applySettings) {
        h.applySettings = [](const web::BoothSettings &s, std::string &error) {
            const WebSettings c = toC(s);
            char buf[128] = "";
            const bool ok = s_hooks.applySettings(&c, buf, static_cast<int>(sizeof(buf)));
            buf[sizeof(buf) - 1] = '\0';
            if (!ok) {
                error = buf;
            }
            return ok;
        };
    }
    if (s_hooks.requestCapture) {
        h.requestCapture = [] { return s_hooks.requestCapture(); };
    }
    if (s_hooks.requestCancel) {
        h.requestCancel = [] { s_hooks.requestCancel(); };
    }
    return h;
}

} // namespace

extern "C" bool WebServer_init(const WebConfig *config, const WebHooks *hooks)
{
    std::lock_guard<std::mutex> guard(s_lock);
    if (s_server) {
        return true;
    }
    try {
        web::WebConfig cfg;
        if (config) {
            if (config->photoDir) cfg.photoDir = config->photoDir;
            if (config->wwwDir) cfg.wwwDir = config->wwwDir;
            if (config->port > 0) cfg.port = config->port;
            cfg.allowDelete = config->allowDelete;
        }
        s_hooks = hooks ? *hooks : WebHooks{};

        s_server = std::make_unique<web::WebServer>(cfg, makeHooks());
        if (!s_server->start()) {
            s_server.reset();
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[web] Failed to start: %s\n", e.what());
        s_server.reset();
        return false;
    }
}

extern "C" void WebServer_cleanup(void)
{
    std::lock_guard<std::mutex> guard(s_lock);
    if (s_server) {
        s_server->stop();
        s_server.reset();
    }
    s_hooks = WebHooks{};
}
