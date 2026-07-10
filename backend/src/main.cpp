#include "json_rpc.h"
#include "config.h"
#include "audio.h"
#include "clipboard.h"
#include "stream_api.h"
#include "hypr.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>

#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>

static std::atomic<bool> running{true};
static std::mutex cout_mutex;
static AppConfig config;
static AudioCapture capture;
static Clipboard clipboard;
static std::unique_ptr<WebSocketStream> stream;
static KeybindManager keybinds;
static bool first_config = true;
static std::chrono::steady_clock::time_point recording_started_at;
// Serialises the shared-state part of finalize (history append + clipboard paste) across
// the detached finalize workers below.
static std::mutex finalize_mutex;
// Guards every keybind op + the config write, since the Hyprland-event listener thread
// (below) re-asserts binds concurrently with the main JSON-RPC thread.
static std::mutex keybind_mutex;
static std::atomic<int> event_fd{-1};
static std::thread event_thread;

static void write_output(const std::string& line) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << line;
    std::cout.flush();
}

// ── transcript history ────────────────────────────────────────────────────────
// Every successful transcription is appended to <pluginDir>/transcripts.jsonl
// (one {"ts","text"} per line, user data — git-ignored like settings.json). The
// settings pane's history picker reads it back via the get_history RPC.

static std::string plugin_dir() {
    // /proc/self/exe -> <pluginDir>/bin/voxflow-backend
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return "";
    buf[n] = '\0';
    std::string p(buf);
    size_t slash = p.rfind('/');           // strip binary name
    if (slash == std::string::npos) return "";
    p.resize(slash);
    slash = p.rfind('/');                  // strip bin/
    if (slash == std::string::npos) return "";
    p.resize(slash);
    return p;
}

static std::string history_path() {
    std::string dir = plugin_dir();
    return dir.empty() ? "" : dir + "/transcripts.jsonl";
}

static std::vector<std::string> read_history_lines() {
    std::vector<std::string> lines;
    std::ifstream in(history_path());
    std::string line;
    while (std::getline(in, line))
        if (!line.empty()) lines.push_back(line);
    return lines;
}

static void append_history(const std::string& text) {
    if (text.empty()) return;
    std::string path = history_path();
    if (path.empty()) return;

    std::vector<std::string> lines = read_history_lines();
    lines.push_back(json{{"ts", std::time(nullptr)}, {"text", text}}.dump());
    // Cap the file: once it grows past 300 entries, keep the newest 200.
    if (lines.size() > 300)
        lines.erase(lines.begin(), lines.end() - 200);

    std::ofstream out(path + ".tmp", std::ios::trunc);
    if (!out) return;
    for (const auto& l : lines) out << l << "\n";
    out.close();
    std::rename((path + ".tmp").c_str(), path.c_str());
}

// Newest first, capped at `limit`.
static json read_history(size_t limit) {
    std::vector<std::string> lines = read_history_lines();
    json items = json::array();
    for (auto it = lines.rbegin(); it != lines.rend() && items.size() < limit; ++it) {
        try {
            json j = json::parse(*it);
            if (j.contains("text"))
                items.push_back({{"ts", j.value("ts", 0)}, {"text", j["text"]}});
        } catch (...) {}
    }
    return items;
}

// The full set of binds the plugin owns: toggle chord(s) + the two history pickers.
static std::vector<Bind> desired_binds() {
    std::vector<Bind> binds;
    for (const auto& chord : config.keybinds)
        binds.push_back({chord, hypr::plugin_cmd("toggleRecording")});
    if (!config.transcript_history_keybind.empty())
        binds.push_back({config.transcript_history_keybind,
                         hypr::plugin_cmd("showTranscriptHistory")});
    if (!config.clipboard_history_keybind.empty())
        binds.push_back({config.clipboard_history_keybind,
                         hypr::plugin_cmd("showClipboardHistory")});
    return binds;
}

static bool start_recording(const json& params) {
    int sr = params.value("sample_rate", config.sample_rate);
    int ch = params.value("channels", 1);

    std::string api_key;
    if (config.provider == "sarvam") {
        api_key = config.sarvam_api_key;
    } else {
        api_key = config.soniox_api_key;
    }

    if (api_key.empty()) {
        write_output(make_event("error", {
            {"message", config.provider + " API key not configured"}
        }) + "\n");
        return false;
    }

    stream = std::make_unique<WebSocketStream>();
    bool ws_ok = stream->open(config.provider, api_key, config.language, sr,
        [](const TranscriptSegment& seg) {
            json data;
            data["text"] = seg.text;
            data["is_final"] = seg.is_final;
            write_output(make_event("transcript", data) + "\n");
        });

    if (!ws_ok) {
        write_output(make_event("error", {
            {"message", "Failed to connect to " + config.provider + " WebSocket"}
        }) + "\n");
        stream.reset();
        return false;
    }

    bool ok = capture.start(sr, ch,
        [](float level) {
            write_output(make_event("level", {{"value", level}}) + "\n");
        },
        [](const int16_t* samples, size_t count) {
            if (stream && stream->is_open()) {
                stream->send_audio(samples, count);
            }
        });

    if (ok) {
        recording_started_at = std::chrono::steady_clock::now();
        write_output(make_event("recording_started", {}) + "\n");
    } else {
        write_output(make_event("error", {
            {"message", "Failed to start audio capture"}
        }) + "\n");
        if (stream) {
            stream->close();
            stream.reset();
        }
    }
    return ok;
}

static void stop_recording(int64_t id) {
    if (!capture.is_recording()) {
        write_output(make_error(id, -1, "Not recording") + "\n");
        return;
    }

    // Cancel (go straight back to idle, don't wait on the server) when the recording
    // carried no transcribable speech:
    //   - too short: a start-then-stop within ~500ms = "never mind", or
    //   - silent:    no meaningful audio the whole time (peak RMS below threshold).
    // The silent case is the important one — otherwise close() blocks this single JSON-RPC
    // thread up to its timeout waiting for a transcript that never comes, which freezes the
    // whole backend (mic appears dead) until it gives up.
    auto elapsed = std::chrono::steady_clock::now() - recording_started_at;
    bool too_short = elapsed < std::chrono::milliseconds(500);
    bool silent = capture.peak_level() < 0.012f;
    if (too_short || silent) {
        capture.stop();
        if (stream) { stream->abort(); stream.reset(); }
        write_output(make_result(id, {{"text", ""}, {"length", 0}, {"cancelled", true}}) + "\n");
        return;
    }

    // Adaptive end-of-speech: users hit the stop chord the instant they *think* they're
    // done, but a trailing word may still be coming out (or in the mic pipeline). Instead
    // of a blind fixed grace, keep the mic open until we actually detect a short trailing
    // silence in the live input RMS — so we never cut off mid-word, yet stop promptly once
    // you've clearly finished. Bounded so a noisy room can't stall the stop.
    constexpr float kSilenceLevel  = 0.012f;  // RMS below this ≈ not speaking
    constexpr int   kSilenceHoldMs = 250;     // continuous silence that confirms "done"
    constexpr int   kMaxWaitMs     = 1500;    // hard cap (noise floor safety)
    constexpr int   kStepMs        = 20;
    int waited = 0, quiet = 0;
    while (waited < kMaxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kStepMs));
        waited += kStepMs;
        if (capture.current_level() < kSilenceLevel) {
            quiet += kStepMs;
            if (quiet >= kSilenceHoldMs) break;   // stopped talking → finish
        } else {
            quiet = 0;                            // still speaking → keep capturing
        }
    }

    capture.stop();   // release the mic now; the device is free for the next recording

    write_output(make_event("processing", {}) + "\n");

    if (!stream || !stream->is_open()) {
        write_output(make_error(id, -2, "WebSocket stream not open") + "\n");
        return;
    }

    // Finalize (close the stream, wait for the transcript, paste) on a DETACHED worker so
    // the JSON-RPC loop stays responsive: close() can take a few seconds — and, on a stuck
    // stream, up to its 12s cap — but the backend must not freeze (that was the mic "going
    // dead"). Move the stream into the worker so a new recording can open a fresh one; the
    // mic was already released above.
    auto owned = std::move(stream);   // global `stream` is now null
    bool append_nl = config.append_newline;
    std::thread([id, append_nl, st = std::move(owned)]() mutable {
        StreamResult result = st->close();
        st.reset();

        if (!result.success) {
            write_output(make_event("error", {{"message", result.error_message}}) + "\n");
            write_output(make_error(id, -4, result.error_message) + "\n");
            return;
        }

        const std::string& text = result.text;
        {
            std::lock_guard<std::mutex> lk(finalize_mutex);
            append_history(text);
            clipboard.copy_and_paste(text, append_nl);
        }
        write_output(make_result(id, {
            {"text", text},
            {"length", text.length()}
        }) + "\n");
    }).detach();
}

static void handle_config(const json& params) {
    {
        std::lock_guard<std::mutex> lock(keybind_mutex);
        config.from_json(params);
        // Register the plugin-owned chords live in Hyprland (toggle + history pickers).
        // Skip while the settings UI is capturing a chord (binds are suspended then; they
        // get reconciled on capture end).
        if (hypr::available() && !keybinds.is_capturing()) {
            if (first_config) keybinds.migrate_hyprland_config();
            keybinds.reconcile(desired_binds(), first_config);
            first_config = false;
        }
    }
    // Only the (single) main thread writes config, so this read needs no lock.
    write_output(make_event("config_updated", config.to_json()) + "\n");
}

// Force-re-register our binds. Called when Hyprland reloads its config, which wipes all
// runtime-registered binds (hl.bind) — without this they silently stop working until the
// next backend restart. The `initial=true` reconcile unbinds-then-binds unconditionally.
static void reassert_binds() {
    std::lock_guard<std::mutex> lock(keybind_mutex);
    if (first_config) return;                    // no user config received yet
    if (!hypr::available() || keybinds.is_capturing()) return;
    keybinds.reconcile(desired_binds(), /*initial=*/true);
}

// Long-lived thread: subscribe to Hyprland's event stream and re-assert binds on
// `configreloaded`. Exits cleanly when `running` is cleared and the fd is shut down.
static void hypr_event_loop() {
    std::string path = hypr::event_socket_path();
    if (path.empty()) return;

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) { ::close(fd); return; }
    std::strcpy(addr.sun_path, path.c_str());
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return;
    }
    event_fd = fd;

    std::string buf;
    char tmp[4096];
    while (running) {
        ssize_t n = ::read(fd, tmp, sizeof(tmp));
        if (n <= 0) break;                       // Hyprland gone, or shutdown() woke us
        buf.append(tmp, static_cast<size_t>(n));
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (line.rfind("configreloaded", 0) == 0) reassert_binds();
        }
    }
    int old = event_fd.exchange(-1);
    if (old >= 0) ::close(old);
}

static void process_line(const std::string& line) {
    RpcRequest req;
    if (!parse_rpc(line, req)) {
        return;
    }

    if (req.method == "start_recording") {
        start_recording(req.params);
    } else if (req.method == "stop_recording") {
        stop_recording(req.id);
    } else if (req.method == "set_config") {
        handle_config(req.params);
        if (req.id) {
            write_output(make_result(req.id, config.to_json()) + "\n");
        }
    } else if (req.method == "set_capture_mode") {
        // The settings UI toggles this around recording a new chord.
        if (hypr::available()) {
            std::lock_guard<std::mutex> lock(keybind_mutex);
            keybinds.set_capture(req.params.value("on", false), desired_binds());
        }
    } else if (req.method == "get_history") {
        // Past transcripts, newest first — feeds the history pickers.
        size_t limit = req.params.value("limit", 100);
        write_output(make_result(req.id, {{"items", read_history(limit)}}) + "\n");
    } else if (req.method == "paste_text") {
        // Paste an old transcript picked from history: same flow as a fresh dictation
        // (clipboard saved, compositor Ctrl+V, clipboard restored).
        std::string text = req.params.value("text", "");
        if (!text.empty()) clipboard.copy_and_paste(text, false);
        if (req.id) write_output(make_result(req.id, {{"ok", !text.empty()}}) + "\n");
    } else if (req.method == "send_paste") {
        // Just the compositor Ctrl+V — the clipboard-history picker has already put
        // the chosen item on the clipboard (cliphist decode | wl-copy).
        bool ok = clipboard.paste();
        if (req.id) write_output(make_result(req.id, {{"ok", ok}}) + "\n");
    } else if (req.method == "ping") {
        write_output(make_result(req.id, {{"pong", true}}) + "\n");
    } else if (req.method == "shutdown") {
        {
            std::lock_guard<std::mutex> lock(keybind_mutex);
            keybinds.clear();
        }
        running = false;
    }
}

// Wake the event listener's blocking read() so the thread can exit, then join it.
static void stop_event_listener() {
    running = false;
    int fd = event_fd.load();
    if (fd >= 0) ::shutdown(fd, SHUT_RDWR);   // unblock read() without closing (no fd reuse)
    if (event_thread.joinable()) event_thread.join();
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    write_output(make_event("ready", {
        {"version", "2.0.0"},
        {"name", "voxflow-backend"},
        {"mode", "streaming"}
    }) + "\n");

    // Re-assert Hyprland binds whenever the config reloads (which wipes runtime binds).
    if (hypr::available()) {
        event_thread = std::thread(hypr_event_loop);
    }

    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        process_line(line);
    }

    stop_event_listener();

    if (capture.is_recording()) {
        capture.stop();
    }
    if (stream) {
        stream->close();
        stream.reset();
    }

    return 0;
}
