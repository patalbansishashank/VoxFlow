#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct AppConfig {
    std::string soniox_api_key;
    std::string sarvam_api_key;
    std::string provider = "soniox";
    std::string language = "en-IN";
    int sample_rate = 16000;
    bool append_newline = true;
    // Chords that toggle recording, in Hyprland form (e.g. "SUPER + Z"). The plugin
    // (not hyprland.lua) owns these — the backend registers them live via hl.bind.
    std::vector<std::string> keybinds = {"SUPER + Z"};
    // Picker chords (empty string = disabled): past transcripts, and general clipboard
    // history with transcripts filtered out. Also plugin-owned, registered live.
    std::string transcript_history_keybind = "SUPER + SHIFT + Z";
    std::string clipboard_history_keybind = "SUPER + V";

    void from_json(const json& j) {
        soniox_api_key = j.value("soniox_api_key", soniox_api_key);
        sarvam_api_key = j.value("sarvam_api_key", sarvam_api_key);
        provider = j.value("provider", provider);
        language = j.value("language", language);
        sample_rate = j.value("sample_rate", sample_rate);
        append_newline = j.value("append_newline", append_newline);
        // Replace wholesale when present (an empty array means "no bind"); keep the
        // default only when the key is absent entirely.
        if (j.contains("keybinds") && j["keybinds"].is_array()) {
            keybinds.clear();
            for (const auto& k : j["keybinds"])
                if (k.is_string()) keybinds.push_back(k.get<std::string>());
        }
        transcript_history_keybind =
            j.value("transcript_history_keybind", transcript_history_keybind);
        clipboard_history_keybind =
            j.value("clipboard_history_keybind", clipboard_history_keybind);
    }

    json to_json() const {
        return {
            {"soniox_api_key", soniox_api_key},
            {"sarvam_api_key", sarvam_api_key},
            {"provider", provider},
            {"language", language},
            {"sample_rate", sample_rate},
            {"append_newline", append_newline},
            {"keybinds", keybinds},
            {"transcript_history_keybind", transcript_history_keybind},
            {"clipboard_history_keybind", clipboard_history_keybind}
        };
    }
};
