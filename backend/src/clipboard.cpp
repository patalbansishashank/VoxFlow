#include "clipboard.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>
#include <array>

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

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    if (!paste()) {
        fprintf(stderr,
            "{\"event\":\"notify\",\"data\":{\"message\":\"Paste failed. "
            "Install wtype (pacman -S wtype) or press Ctrl+V manually\"}}\n");
        fflush(stderr);
        if (!prev.empty()) restore(prev);
        return false;
    }

    if (!prev.empty()) {
        std::thread([prev = std::move(prev)]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            run_command_stdin("wl-copy 2>/dev/null", prev);
        }).detach();
    }

    return true;
}
