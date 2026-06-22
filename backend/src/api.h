#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ApiResult {
    bool success = false;
    std::string text;
    int error_code = 0;
    std::string error_message;
};

class ApiClient {
public:
    ApiResult transcribe(const std::string& provider,
                         const std::vector<uint8_t>& wav_data,
                         const std::string& api_key,
                         const std::string& language);

private:
    ApiResult transcribe_soniox(const std::vector<uint8_t>& wav_data,
                                 const std::string& api_key,
                                 const std::string& language);

    ApiResult transcribe_sarvam(const std::vector<uint8_t>& wav_data,
                                 const std::string& api_key,
                                 const std::string& language);

    ApiResult transcribe_sarvam_batch(const std::vector<uint8_t>& wav_data,
                                       const std::string& api_key,
                                       const std::string& language);

    static double get_audio_duration(const std::vector<uint8_t>& wav_data);
    static std::string post_json(const std::string& url,
                                  const std::string& body,
                                  const std::string& api_key,
                                  long* http_code_out = nullptr);
    static std::string put_data(const std::string& url,
                                 const std::vector<uint8_t>& data,
                                 long* http_code_out = nullptr);
    static std::string get_url(const std::string& url,
                                const std::string& api_key);

    static std::string url_encode(const std::string& s);
};
