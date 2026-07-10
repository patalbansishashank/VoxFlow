#include "clipboard.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <array>
#include <algorithm>

static bool run_command(const char* cmd) {
    int ret = std::system(cmd);
    return ret == 0;
}

static bool run_command_stdin(const char* cmd, const std::string& input) {
    FILE* pipe = popen(cmd, "w");
    if (!pipe) return false;
    if (!input.empty()) {
        fwrite(input.data(), 1, input.size(), pipe);
    }
    int ret = pclose(pipe);
    return ret == 0;
}

static std::string run_command_stdout(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return {};

    std::string result;
    std::array<char, 4096> buf;
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        result.append(buf.data(), n);
    }
    pclose(pipe);
    return result;
}

// wl-paste appends a trailing newline unless -n; strip trailing EOL bytes so clipboard
// comparisons are newline-insensitive.
static std::string strip_trailing_eol(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

// Read the current clipboard selection without wl-paste's added trailing newline.
static std::string read_clipboard_now() {
    return strip_trailing_eol(run_command_stdout("wl-paste -n 2>/dev/null || true"));
}

Clipboard::Clipboard() = default;

bool Clipboard::copy(const std::string& text) {
    return run_command_stdin("wl-copy 2>/dev/null", text);
}

std::string Clipboard::save() {
    return run_command_stdout("timeout 2 wl-paste 2>/dev/null || true");
}

bool Clipboard::restore(const std::string& previous) {
    if (previous.empty()) return false;
    return copy(previous);
}

bool Clipboard::paste_via_wtype() {
    return run_command("wtype -M ctrl -k v -m ctrl 2>/dev/null");
}

bool Clipboard::paste_via_ydotool() {
    return run_command("ydotool key 29:1 47:1 47:0 29:0 2>/dev/null");
}

bool Clipboard::paste() {
    if (paste_via_wtype()) return true;
    if (paste_via_ydotool()) return true;
    return false;
}

bool Clipboard::copy_and_paste(const std::string& text, bool append_newline) {
    std::string payload = text;

    if (!payload.empty() && payload.back() == '\n') {
        payload.pop_back();
    }
    if (append_newline) {
        payload += '\n';
    }

    if (payload.empty()) return false;

    std::string prev = save();

    if (!copy(payload)) return false;

    // Confirm the compositor has actually made our text the live selection BEFORE we
    // send Ctrl+V. A fixed sleep races: on a busy system or with a large payload the
    // keystroke can fire before the new selection is registered, so the app pastes
    // stale clipboard contents. Poll wl-paste until it reports our text.
    const std::string want = strip_trailing_eol(payload);
    bool ready = false;
    for (int i = 0; i < 40; ++i) {  // up to ~800ms
        if (read_clipboard_now() == want) { ready = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!ready) {
        // Couldn't confirm; settle a bit longer than the old 60ms and try anyway.
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }

    if (!paste()) {
        fprintf(stderr,
            "{\"event\":\"notify\",\"data\":{\"message\":\"Paste failed. "
            "Install wtype (pacman -S wtype) or press Ctrl+V manually\"}}\n");
        fflush(stderr);
        if (!prev.empty()) restore(prev);
        return false;
    }

    // Restore the user's previous clipboard, but only after the target app has had time
    // to read our selection. Terminals read the Wayland selection synchronously and fast;
    // browsers (Chromium/Firefox) read it lazily and much slower, so a short delay clobbers
    // our text mid-read -> failed/partial paste (worse for long transcripts = bigger
    // transfer). Scale the delay with payload size on top of a generous floor, and skip
    // the restore entirely if something newer has since taken the clipboard (rapid
    // re-dictation, or the user copied something else) so we never clobber it.
    if (!prev.empty()) {
        int delay_ms = 1500 + static_cast<int>(std::min<size_t>(payload.size() * 2, 4000));
        std::thread([prev = std::move(prev), want, delay_ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            if (read_clipboard_now() == want) {
                run_command_stdin("wl-copy 2>/dev/null", prev);
            }
        }).detach();
    }

    return true;
}
