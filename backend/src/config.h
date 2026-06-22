#pragma once

#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct AppConfig {
    std::string soniox_api_key;
    std::string sarvam_api_key;
    std::string provider = "soniox";
    std::string language = "en-IN";
    int sample_rate = 16000;
    bool append_newline = true;

    void from_json(const json& j) {
        soniox_api_key = j.value("soniox_api_key", soniox_api_key);
        sarvam_api_key = j.value("sarvam_api_key", sarvam_api_key);
        provider = j.value("provider", provider);
        language = j.value("language", language);
        sample_rate = j.value("sample_rate", sample_rate);
        append_newline = j.value("append_newline", append_newline);
    }

    json to_json() const {
        return {
            {"soniox_api_key", soniox_api_key},
            {"sarvam_api_key", sarvam_api_key},
            {"provider", provider},
            {"language", language},
            {"sample_rate", sample_rate},
            {"append_newline", append_newline}
        };
    }
};
