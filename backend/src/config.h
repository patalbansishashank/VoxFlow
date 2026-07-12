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
    // Azure (provider == "azure"). `azure_model` selects the model AND the path: every value
    // except "openai-streaming" is a batch model on the Azure Speech "LLM Speech" REST API
    // (mai-transcribe-1.5 / mai-transcribe-1 / llm-enhanced / fast) reached with the Speech
    // resource creds; "openai-streaming" is the Azure OpenAI realtime WebSocket, reached with
    // the OpenAI resource creds + a deployment name. The user only fills the creds for the
    // service their selected model belongs to.
    std::string azure_model = "mai-transcribe-1.5";
    std::string azure_speech_endpoint;     // https://NAME.cognitiveservices.azure.com
    std::string azure_speech_key;          // Ocp-Apim-Subscription-Key
    std::string azure_openai_endpoint;     // https://NAME.openai.azure.com
    std::string azure_openai_key;          // api-key
    std::string azure_openai_deployment;   // gpt-4o-transcribe deployment name
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
        azure_model = j.value("azure_model", azure_model);
        azure_speech_endpoint = j.value("azure_speech_endpoint", azure_speech_endpoint);
        azure_speech_key = j.value("azure_speech_key", azure_speech_key);
        azure_openai_endpoint = j.value("azure_openai_endpoint", azure_openai_endpoint);
        azure_openai_key = j.value("azure_openai_key", azure_openai_key);
        azure_openai_deployment = j.value("azure_openai_deployment", azure_openai_deployment);
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
            {"azure_model", azure_model},
            {"azure_speech_endpoint", azure_speech_endpoint},
            {"azure_speech_key", azure_speech_key},
            {"azure_openai_endpoint", azure_openai_endpoint},
            {"azure_openai_key", azure_openai_key},
            {"azure_openai_deployment", azure_openai_deployment},
            {"keybinds", keybinds},
            {"transcript_history_keybind", transcript_history_keybind},
            {"clipboard_history_keybind", clipboard_history_keybind}
        };
    }
};
