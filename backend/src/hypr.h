#pragma once

#include <string>
#include <vector>

// Thin client for the running Hyprland IPC socket. VoxFlow uses it to register its
// push-to-talk toggle chord live (`eval hl.bind(...)`), the same mechanism HyperZone
// uses — this machine runs Hyprland's non-legacy Lua parser, where `hyprctl keyword
// bind` is rejected and only socket `eval` works.
namespace hypr {
    // "$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock", or "" if the
    // env vars are missing (i.e. not running under Hyprland).
    std::string socket_path();
    bool available();
    // Send a raw request (e.g. "eval hl.bind(...)") and return Hyprland's reply
    // ("ok" on success, "" if the socket could not be reached).
    std::string request(const std::string& cmd);
}

// Owns VoxFlow's Hyprland keybinds. The plugin — not hyprland.lua — is the source of
// truth: chords live in the plugin's settings.json and the backend registers them live.
class KeybindManager {
public:
    // Reconcile the live binds to `desired`. On the first call (initial=true) each chord
    // is unbound-then-bound to clear any duplicate already loaded in the running Hyprland
    // (e.g. a hand-written hyprland.lua bind still active from login).
    void reconcile(const std::vector<std::string>& desired, bool initial);
    // Suspend (on=true) / restore (on=false) all live binds while the settings UI records
    // a new chord, so pressing an already-bound chord reaches the recorder instead of
    // firing the toggle. On restore, binds are reconciled to `desired`.
    void set_capture(bool on, const std::vector<std::string>& desired);
    bool is_capturing() const { return captured_; }
    // Comment out any hand-written `hl.bind(... plugin:voxflow ...)` line in
    // ~/.config/hypr/hyprland.lua (a one-time backup is kept) so the plugin fully owns
    // the chord across Hyprland reloads/logins. Idempotent; no-op off Hyprland.
    void migrate_hyprland_config();
    // Best-effort removal of our live binds (on graceful shutdown).
    void clear();

private:
    void bind(const std::string& chord);
    void unbind(const std::string& chord);
    std::vector<std::string> active_;
    bool captured_ = false;
};
