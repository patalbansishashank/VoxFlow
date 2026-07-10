#include "hypr.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
// The command a bound chord runs: toggle VoxFlow recording via Noctalia's plugin IPC.
const char* kToggleCmd = "qs -c noctalia-shell ipc call plugin:voxflow toggleRecording";
}

std::string hypr::socket_path() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    const char* his = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!xdg || !his || !*xdg || !*his) return "";
    return std::string(xdg) + "/hypr/" + his + "/.socket.sock";
}

bool hypr::available() {
    std::string p = socket_path();
    if (p.empty()) return false;
    struct stat st{};
    return ::stat(p.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

std::string hypr::request(const std::string& cmd) {
    std::string path = socket_path();
    if (path.empty()) return "";

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "";

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) { ::close(fd); return ""; }
    std::strcpy(addr.sun_path, path.c_str());

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return "";
    }

    ssize_t off = 0;
    while (off < static_cast<ssize_t>(cmd.size())) {
        ssize_t w = ::write(fd, cmd.data() + off, cmd.size() - off);
        if (w <= 0) break;
        off += w;
    }

    std::string reply;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) reply.append(buf, static_cast<size_t>(n));
    ::close(fd);
    return reply;
}

bool hypr::send_ctrl_v() {
    if (!available()) return false;
    // No `window` arg -> the focused window, i.e. wherever the user's cursor is when
    // they stop dictating. Verified live on this Hyprland (0.55, Lua parser).
    std::string reply = request(
        "eval hl.dispatch(hl.dsp.send_shortcut({ mods = \"CTRL\", key = \"V\" }))");
    return reply.rfind("ok", 0) == 0;
}

void KeybindManager::bind(const std::string& chord) {
    if (chord.empty()) return;
    hypr::request("eval hl.bind(\"" + chord + "\", hl.dsp.exec_cmd(\"" + kToggleCmd + "\"))");
}

void KeybindManager::unbind(const std::string& chord) {
    if (chord.empty()) return;
    hypr::request("eval hl.unbind(\"" + chord + "\")");
}

void KeybindManager::reconcile(const std::vector<std::string>& desired, bool initial) {
    if (initial) {
        // Clear any duplicate the running Hyprland already has (hyprland.lua bind loaded
        // at login), then assert ours.
        for (const auto& c : desired) { unbind(c); bind(c); }
        active_ = desired;
        return;
    }
    for (const auto& c : active_)
        if (std::find(desired.begin(), desired.end(), c) == desired.end()) unbind(c);
    for (const auto& c : desired)
        if (std::find(active_.begin(), active_.end(), c) == active_.end()) bind(c);
    active_ = desired;
}

void KeybindManager::set_capture(bool on, const std::vector<std::string>& desired) {
    if (on == captured_) return;
    captured_ = on;
    if (on) {
        // Physically suspend every live bind; keep active_ so we know what we had.
        for (const auto& c : active_) unbind(c);
    } else {
        // Everything is physically unbound now — rebind to the (possibly changed) desired set.
        active_.clear();
        reconcile(desired, false);
    }
}

void KeybindManager::clear() {
    for (const auto& c : active_) unbind(c);
    active_.clear();
}

void KeybindManager::migrate_hyprland_config() {
    const char* home = std::getenv("HOME");
    if (!home) return;
    std::string path = std::string(home) + "/.config/hypr/hyprland.lua";

    std::ifstream in(path);
    if (!in) return;

    std::vector<std::string> lines;
    std::string line;
    bool changed = false;
    while (std::getline(in, line)) {
        size_t p = line.find_first_not_of(" \t");
        bool commented = (p != std::string::npos && line.compare(p, 2, "--") == 0);
        if (!commented &&
            line.find("hl.bind") != std::string::npos &&
            line.find("plugin:voxflow") != std::string::npos) {
            line = "-- voxflow-managed keybind (now set in the plugin's settings): " + line;
            changed = true;
        }
        lines.push_back(line);
    }
    in.close();
    if (!changed) return;

    // One-time backup of the original before we rewrite it.
    std::string bak = path + ".voxflow-kb-backup";
    struct stat st{};
    if (::stat(bak.c_str(), &st) != 0) {
        std::ifstream src(path, std::ios::binary);
        std::ofstream dst(bak, std::ios::binary);
        dst << src.rdbuf();
    }

    std::ofstream out(path, std::ios::trunc);
    for (const auto& l : lines) out << l << "\n";
}
