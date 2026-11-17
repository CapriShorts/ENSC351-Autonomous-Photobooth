// WebServer.cpp
// Author: Ambardeep Chahal, AEPB team (ENSC 351, Fall 2026)

#include "WebServer.h"

#include "httplib.h"

#include <sys/socket.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace web {
namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

long long nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string &s, const std::string &suffix)
{
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Plain .jpg/.jpeg/.png names only (rejects path traversal and .tmp files).
bool isSafePhotoName(const std::string &name)
{
    if (name.empty() || name.size() > 128 || name[0] == '.') {
        return false;
    }
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    const std::string lower = toLower(name);
    return endsWith(lower, ".jpg") || endsWith(lower, ".jpeg") || endsWith(lower, ".png");
}

std::string jsonEscape(const std::string &in)
{
    std::string out;
    out.reserve(in.size() + 2);
    for (unsigned char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}

std::string q(const std::string &s) { return "\"" + jsonEscape(s) + "\""; }
const char *b(bool v) { return v ? "true" : "false"; }

void sendJson(httplib::Response &res, int status, const std::string &body)
{
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(body, "application/json");
}

void sendError(httplib::Response &res, int status, const std::string &message)
{
    sendJson(res, status, "{\"error\":" + q(message) + "}");
}

bool parseInt(const std::string &text, int &out)
{
    try {
        size_t used = 0;
        int value = std::stoi(text, &used);
        if (used != text.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool parseBool(const std::string &text, bool &out)
{
    const std::string t = toLower(text);
    if (t == "true" || t == "1" || t == "on" || t == "yes") { out = true; return true; }
    if (t == "false" || t == "0" || t == "off" || t == "no") { out = false; return true; }
    return false;
}

bool isHexColor(const std::string &s)
{
    if (s.size() != 7 || s[0] != '#') {
        return false;
    }
    return std::all_of(s.begin() + 1, s.end(),
                       [](unsigned char c) { return std::isxdigit(c) != 0; });
}

// ---------------------------------------------------------------------------
// Photo folder
// ---------------------------------------------------------------------------

struct PhotoInfo {
    std::string name;
    long long sizeBytes = 0;
    long long mtime = 0; // seconds since epoch
};

std::vector<PhotoInfo> listPhotos(const std::string &dir)
{
    std::vector<PhotoInfo> photos;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        return photos;
    }
    for (const auto &entry : it) {
        const std::string name = entry.path().filename().string();
        if (!isSafePhotoName(name)) {
            continue;
        }
        struct stat st {};
        if (::stat(entry.path().c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        photos.push_back({name, static_cast<long long>(st.st_size),
                          static_cast<long long>(st.st_mtime)});
    }
    // Newest first; file name breaks ties so the order is stable.
    std::sort(photos.begin(), photos.end(), [](const PhotoInfo &a, const PhotoInfo &b) {
        if (a.mtime != b.mtime) {
            return a.mtime > b.mtime;
        }
        return a.name > b.name;
    });
    return photos;
}

std::string photoJson(const PhotoInfo &p)
{
    std::ostringstream o;
    o << "{\"name\":" << q(p.name) << ",\"url\":" << q("/photos/" + p.name)
      << ",\"size\":" << p.sizeBytes << ",\"time\":" << p.mtime << "}";
    return o.str();
}

std::string settingsJson(const BoothSettings &s)
{
    std::ostringstream o;
    o << "{\"countdownSeconds\":" << s.countdownSeconds
      << ",\"flashMode\":" << q(s.flashMode)
      << ",\"ringColor\":" << q(s.ringColor)
      << ",\"ringBrightness\":" << s.ringBrightness
      << ",\"soundEnabled\":" << b(s.soundEnabled)
      << ",\"gestureEnabled\":" << b(s.gestureEnabled) << "}";
    return o.str();
}

// Applies request fields to s. Returns false with an error on invalid input.
bool mergeSettings(const httplib::Request &req, BoothSettings &s, std::string &error)
{
    int n = 0;
    bool flag = false;

    if (req.has_param("countdownSeconds")) {
        if (!parseInt(req.get_param_value("countdownSeconds"), n) || n < 1 || n > 10) {
            error = "Countdown must be a whole number from 1 to 10 seconds.";
            return false;
        }
        s.countdownSeconds = n;
    }
    if (req.has_param("flashMode")) {
        const std::string mode = toLower(req.get_param_value("flashMode"));
        if (mode != "auto" && mode != "on" && mode != "off") {
            error = "Flash must be auto, on, or off.";
            return false;
        }
        s.flashMode = mode;
    }
    if (req.has_param("ringColor")) {
        const std::string color = toLower(req.get_param_value("ringColor"));
        if (!isHexColor(color)) {
            error = "Ring colour must look like #ffaa00.";
            return false;
        }
        s.ringColor = color;
    }
    if (req.has_param("ringBrightness")) {
        if (!parseInt(req.get_param_value("ringBrightness"), n) || n < 0 || n > 100) {
            error = "Ring brightness must be from 0 to 100.";
            return false;
        }
        s.ringBrightness = n;
    }
    if (req.has_param("soundEnabled")) {
        if (!parseBool(req.get_param_value("soundEnabled"), flag)) {
            error = "Countdown sound must be true or false.";
            return false;
        }
        s.soundEnabled = flag;
    }
    if (req.has_param("gestureEnabled")) {
        if (!parseBool(req.get_param_value("gestureEnabled"), flag)) {
            error = "Gesture control must be true or false.";
            return false;
        }
        s.gestureEnabled = flag;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// WebServer
// ---------------------------------------------------------------------------

WebServer::WebServer(WebConfig config, BoothHooks hooks)
    : config_(std::move(config)), hooks_(std::move(hooks))
{
}

WebServer::~WebServer() { stop(); }

bool WebServer::isRunning() const { return server_ && server_->is_running(); }

bool WebServer::start()
{
    if (server_) {
        return isRunning();
    }

    std::error_code ec;
    fs::create_directories(config_.photoDir, ec);
    if (!fs::is_directory(config_.photoDir)) {
        std::fprintf(stderr, "[web] Photo folder %s doesn't exist and couldn't be created.\n",
                     config_.photoDir.c_str());
        return false;
    }
    if (!fs::is_directory(config_.wwwDir)) {
        std::fprintf(stderr, "[web] Website folder %s not found. Copy www/ next to the program.\n",
                     config_.wwwDir.c_str());
        return false;
    }

    server_ = std::make_unique<httplib::Server>();

    // Small thread pool to leave CPU for the other threads.
    const size_t workers = static_cast<size_t>(std::max(1, config_.workerThreads));
    server_->new_task_queue = [workers] { return new httplib::ThreadPool(workers, workers * 2); };

    // SO_REUSEADDR only, so a second instance fails to bind.
    server_->set_socket_options([](socket_t sock) {
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
    });

    server_->set_keep_alive_timeout(5);
    server_->set_payload_max_length(64 * 1024); // API bodies are tiny

    // Log failed requests only.
    server_->set_logger([](const httplib::Request &req, const httplib::Response &res) {
        if (res.status >= 400) {
            std::fprintf(stderr, "[web] %s %s -> %d\n", req.method.c_str(), req.path.c_str(),
                         res.status);
        }
    });

    registerRoutes();

    // Photos don't change once saved, so allow caching.
    if (!server_->set_mount_point("/photos", config_.photoDir,
                                  {{"Cache-Control", "public, max-age=86400"}})) {
        std::fprintf(stderr, "[web] Couldn't serve photos from %s\n", config_.photoDir.c_str());
        server_.reset();
        return false;
    }
    // Website files: always revalidate.
    if (!server_->set_mount_point("/", config_.wwwDir, {{"Cache-Control", "no-cache"}})) {
        std::fprintf(stderr, "[web] Couldn't serve website from %s\n", config_.wwwDir.c_str());
        server_.reset();
        return false;
    }

    if (!server_->bind_to_port(config_.host, config_.port)) {
        std::fprintf(stderr, "[web] Couldn't listen on %s:%d. Is another copy already running?\n",
                     config_.host.c_str(), config_.port);
        server_.reset();
        return false;
    }

    startTimeMs_ = nowMs();
    thread_ = std::thread([this] { server_->listen_after_bind(); });
    server_->wait_until_ready();

    std::printf("[web] Photo booth website on http://%s:%d  (photos: %s)\n",
                config_.host == "0.0.0.0" ? "<board-ip>" : config_.host.c_str(), config_.port,
                config_.photoDir.c_str());
    return true;
}

void WebServer::stop()
{
    if (server_) {
        server_->stop();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    server_.reset();
}

void WebServer::registerRoutes()
{
    auto &svr = *server_;

    // ---- Photos -----------------------------------------------------------

    // GET /api/photos[?limit=N]  -> newest first
    svr.Get("/api/photos", [this](const httplib::Request &req, httplib::Response &res) {
        auto photos = listPhotos(config_.photoDir);
        int limit = 0;
        if (req.has_param("limit") && parseInt(req.get_param_value("limit"), limit) &&
            limit > 0 && static_cast<size_t>(limit) < photos.size()) {
            photos.resize(static_cast<size_t>(limit));
        }
        std::ostringstream o;
        o << "{\"photos\":[";
        for (size_t i = 0; i < photos.size(); ++i) {
            o << (i ? "," : "") << photoJson(photos[i]);
        }
        o << "]}";
        sendJson(res, 200, o.str());
    });

    // DELETE /api/photos/<name>
    svr.Delete("/api/photos/:name", [this](const httplib::Request &req, httplib::Response &res) {
        if (!config_.allowDelete) {
            sendError(res, 403, "Deleting photos is turned off on this booth.");
            return;
        }
        const std::string name = req.path_params.at("name");
        if (!isSafePhotoName(name)) {
            sendError(res, 400, "That isn't a photo name.");
            return;
        }
        std::error_code ec;
        if (!fs::remove(fs::path(config_.photoDir) / name, ec)) {
            sendError(res, 404, "Photo not found. It may already be deleted.");
            return;
        }
        std::printf("[web] Deleted photo %s\n", name.c_str());
        sendJson(res, 200, "{\"ok\":true}");
    });

    // ---- Status (polled by the page) --------------------------------------

    svr.Get("/api/status", [this](const httplib::Request &, httplib::Response &res) {
        // Works without the hook so the gallery still sees new photos.
        BoothStatus st;
        if (hooks_.getStatus) {
            st = hooks_.getStatus();
        } else {
            st.state = "UNKNOWN";
        }
        const auto photos = listPhotos(config_.photoDir);

        std::ostringstream o;
        o << "{\"state\":" << q(st.state)
          << ",\"countdownRemaining\":" << st.countdownRemaining
          << ",\"countdownTotal\":" << st.countdownTotal
          << ",\"lightLevel\":" << st.lightLevel
          << ",\"flashWillFire\":" << b(st.flashWillFire)
          << ",\"message\":" << q(st.message)
          << ",\"photoCount\":" << photos.size()
          << ",\"latestPhoto\":" << (photos.empty() ? std::string("null") : q(photos[0].name))
          << ",\"uptimeSeconds\":" << (nowMs() - startTimeMs_) / 1000
          << ",\"features\":{"
          << "\"status\":" << b(static_cast<bool>(hooks_.getStatus))
          << ",\"settings\":" << b(hooks_.getSettings && hooks_.applySettings)
          << ",\"capture\":" << b(static_cast<bool>(hooks_.requestCapture))
          << ",\"cancel\":" << b(static_cast<bool>(hooks_.requestCancel))
          << ",\"delete\":" << b(config_.allowDelete) << "}}";
        sendJson(res, 200, o.str());
    });

    // ---- Settings ---------------------------------------------------------

    svr.Get("/api/settings", [this](const httplib::Request &, httplib::Response &res) {
        if (!hooks_.getSettings) {
            sendError(res, 501, "Settings aren't connected yet.");
            return;
        }
        sendJson(res, 200, settingsJson(hooks_.getSettings()));
    });

    // POST /api/settings (form-encoded, partial updates allowed)
    svr.Post("/api/settings", [this](const httplib::Request &req, httplib::Response &res) {
        if (!hooks_.getSettings || !hooks_.applySettings) {
            sendError(res, 501, "Settings aren't connected yet.");
            return;
        }
        BoothSettings updated = hooks_.getSettings();
        std::string error;
        if (!mergeSettings(req, updated, error)) {
            sendError(res, 400, error);
            return;
        }
        if (!hooks_.applySettings(updated, error)) {
            sendError(res, 409, error.empty() ? "The booth rejected those settings." : error);
            return;
        }
        sendJson(res, 200, settingsJson(hooks_.getSettings()));
    });

    // ---- Remote buttons ---------------------------------------------------

    svr.Post("/api/capture", [this](const httplib::Request &, httplib::Response &res) {
        if (!hooks_.requestCapture) {
            sendError(res, 501, "Remote capture isn't connected yet.");
            return;
        }
        if (!hooks_.requestCapture()) {
            sendError(res, 409, "The booth is busy. Wait for the current photo to finish.");
            return;
        }
        sendJson(res, 202, "{\"ok\":true}");
    });

    svr.Post("/api/cancel", [this](const httplib::Request &, httplib::Response &res) {
        if (!hooks_.requestCancel) {
            sendError(res, 501, "Remote cancel isn't connected yet.");
            return;
        }
        hooks_.requestCancel();
        sendJson(res, 200, "{\"ok\":true}");
    });
}

} // namespace web
