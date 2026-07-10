#include "clipboard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <array>
#include <algorithm>
#include <vector>
#include <unistd.h>

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
    // Native-Wayland Chromium/Electron are strict about wtype's synthetic input: they
    // drop the Ctrl+V when the freshly-created virtual keyboard's keymap isn't active
    // yet, or when Ctrl and V arrive in the same instant. Lenient apps (Alacritty)
    // tolerate the fast path, which is why the terminal pasted but the browser didn't.
    // Settle after the keyboard is created (-s 90), then hold Ctrl with small gaps
    // around an explicit press/release of V so the modifier is reliably applied.
    return run_command(
        "wtype -s 90 -M ctrl -s 24 -P v -s 24 -p v -s 24 -m ctrl 2>/dev/null");
}

// Locate a running ydotoold socket, if any. ydotool injects input at the kernel
// (uinput) level, which native-Wayland Chromium/Electron accept — where wtype's
// virtual-keyboard keystrokes are silently dropped. Empty string => not running.
static std::string ydotool_socket() {
    if (const char* env = std::getenv("YDOTOOL_SOCKET"))
        if (env[0] && access(env, F_OK) == 0) return env;
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::vector<std::string> candidates = {"/run/ydotoold.sock"};
    if (rt && rt[0]) candidates.push_back(std::string(rt) + "/.ydotool_socket");
    candidates.push_back("/run/user/1000/.ydotool_socket");
    candidates.push_back("/tmp/.ydotool_socket");
    for (const auto& p : candidates)
        if (access(p.c_str(), F_OK) == 0) return p;
    return "";
}

bool Clipboard::paste_via_ydotool() {
    const std::string sock = ydotool_socket();
    if (sock.empty()) return false;  // no daemon -> let paste() fall back to wtype
    // 29 = KEY_LEFTCTRL, 47 = KEY_V: press ctrl, press v, release v, release ctrl.
    const std::string cmd =
        "YDOTOOL_SOCKET=" + sock + " ydotool key 29:1 47:1 47:0 29:0 2>/dev/null";
    return run_command(cmd.c_str());
}

bool Clipboard::paste() {
    // Prefer ydotool: kernel-level input that Chromium/Electron accept. Fall back to
    // wtype (Wayland virtual keyboard) when ydotoold isn't installed/running — that
    // still works for lenient native-Wayland apps (terminals, most GTK/Qt).
    if (paste_via_ydotool()) return true;
    if (paste_via_wtype()) return true;
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
