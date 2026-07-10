#include "clipboard.h"
#include "hypr.h"

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

// MIME types currently offered by the clipboard owner, one per line.
static std::vector<std::string> list_clipboard_types() {
    std::string out =
        run_command_stdout("timeout 2 wl-paste --list-types 2>/dev/null || true");
    std::vector<std::string> types;
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find('\n', start);
        if (end == std::string::npos) end = out.size();
        if (end > start) types.emplace_back(out.substr(start, end - start));
        start = end + 1;
    }
    return types;
}

// Pick the single most useful type to preserve. wl-copy can only re-offer one type,
// so a rich copy (e.g. text/html + text/plain) is restored as its plain-text form —
// the content survives, only the formatting sidecar is dropped.
static std::string pick_clipboard_type(const std::vector<std::string>& types) {
    auto has = [&](const std::string& t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    };
    if (has("text/plain;charset=utf-8")) return "text/plain;charset=utf-8";
    if (has("text/plain")) return "text/plain";
    for (const auto& t : types)
        if (t.rfind("text/", 0) == 0) return t;
    if (has("UTF8_STRING")) return "UTF8_STRING";
    if (has("STRING")) return "STRING";
    if (has("image/png")) return "image/png";
    for (const auto& t : types)
        if (t.rfind("image/", 0) == 0) return t;
    return types.empty() ? "" : types.front();
}

// Restore a saved selection as its original MIME type. Free function so the detached
// restore thread can use it without holding a Clipboard reference.
static bool restore_saved(const ClipSaved& prev) {
    if (prev.empty()) return false;
    // The type goes into a shell command line; it came from wl-paste --list-types, but
    // refuse anything that could escape the quoting.
    if (prev.mime.find_first_of("'\\\n") != std::string::npos) return false;
    std::string cmd = "wl-copy -t '" + prev.mime + "' 2>/dev/null";
    return run_command_stdin(cmd.c_str(), prev.data);
}

Clipboard::Clipboard() = default;

bool Clipboard::copy(const std::string& text) {
    return run_command_stdin("wl-copy 2>/dev/null", text);
}

ClipSaved Clipboard::save() {
    // Preserve whatever is on the clipboard — text, image, anything — by capturing the
    // exact bytes of its most useful MIME type. The old text-only read came back empty
    // for non-text content (a copied image), so nothing was ever restored.
    std::vector<std::string> types = list_clipboard_types();
    std::string mime = pick_clipboard_type(types);
    if (mime.empty() || mime.find_first_of("'\\\n") != std::string::npos) return {};
    std::string cmd = "timeout 3 wl-paste -n -t '" + mime + "' 2>/dev/null || true";
    std::string data = run_command_stdout(cmd.c_str());
    if (data.empty()) return {};
    return {mime, data};
}

bool Clipboard::restore(const ClipSaved& previous) {
    return restore_saved(previous);
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
    // Injection preference, most- to least-compatible:
    //  1. Hyprland send_shortcut — the compositor synthesizes Ctrl+V on the real seat,
    //     indistinguishable from the physical keyboard; Chromium/Electron accept it
    //     (verified live). No root, no extra daemon.
    //  2. ydotool — kernel-level uinput, also universally accepted, but needs ydotoold.
    //  3. wtype — Wayland virtual keyboard; lenient apps only (terminals, most GTK/Qt);
    //     native-Wayland Chromium/Electron silently drop it.
    if (hypr::send_ctrl_v()) return true;
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

    ClipSaved prev = save();

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
        if (!prev.empty()) restore_saved(prev);
        return false;
    }

    // Restore the user's previous clipboard (text, image, ...), but only after the target
    // app has had time to read our selection. Terminals read the Wayland selection
    // synchronously and fast; browsers (Chromium/Firefox) read it lazily and much slower,
    // so a short delay clobbers our text mid-read -> failed/partial paste (worse for long
    // transcripts = bigger transfer). Scale the delay with payload size on top of a
    // generous floor, and skip the restore entirely if something newer has since taken
    // the clipboard (rapid re-dictation, or the user copied something else) so we never
    // clobber it.
    if (!prev.empty()) {
        int delay_ms = 1500 + static_cast<int>(std::min<size_t>(payload.size() * 2, 4000));
        std::thread([prev = std::move(prev), want, delay_ms]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            if (read_clipboard_now() == want) {
                restore_saved(prev);
            }
        }).detach();
    }

    return true;
}
