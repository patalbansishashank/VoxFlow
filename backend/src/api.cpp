#include "api.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <thread>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <memory>

using json = nlohmann::json;

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), total);
    return total;
}

struct CurlDeleter {
    void operator()(CURL* c) const { if (c) curl_easy_cleanup(c); }
    void operator()(curl_slist* s) const { if (s) curl_slist_free_all(s); }
    void operator()(curl_mime* m) const { if (m) curl_mime_free(m); }
};

using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;
using CurlHeaders = std::unique_ptr<curl_slist, CurlDeleter>;
using CurlMime = std::unique_ptr<curl_mime, CurlDeleter>;

static CurlHandle make_handle() {
    CurlHandle h(curl_easy_init());
    if (h) {
        curl_easy_setopt(h.get(), CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(h.get(), CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(h.get(), CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(h.get(), CURLOPT_USERAGENT, "VoxFlow/1.0");
    }
    return h;
}

static CurlHeaders make_auth_header(const std::string& key) {
    CurlHeaders h;
    std::string auth = "Authorization: Bearer " + key;
    h.reset(curl_slist_append(h.release(), auth.c_str()));
    return h;
}

ApiResult ApiClient::transcribe(const std::string& provider,
                                 const std::vector<uint8_t>& wav_data,
                                 const std::string& api_key,
                                 const std::string& language) {
    if (provider == "sarvam") {
        double duration = get_audio_duration(wav_data);
        if (duration > 30.0) {
            return transcribe_sarvam_batch(wav_data, api_key, language);
        }
        return transcribe_sarvam(wav_data, api_key, language);
    }
    return transcribe_soniox(wav_data, api_key, language);
}

static void add_mime_part(curl_mime* mime, const char* name, const std::string& data) {
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, name);
    curl_mime_data(part, data.c_str(), CURL_ZERO_TERMINATED);
}

static void add_mime_file(curl_mime* mime, const char* name,
                           const std::vector<uint8_t>& data) {
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, name);
    curl_mime_filename(part, "audio.wav");
    curl_mime_type(part, "audio/wav");
    curl_mime_data(part, reinterpret_cast<const char*>(data.data()), data.size());
}

ApiResult ApiClient::transcribe_sarvam(const std::vector<uint8_t>& wav_data,
                                        const std::string& api_key,
                                        const std::string& language) {
    ApiResult result;

    auto curl = make_handle();
    if (!curl) {
        result.error_message = "Failed to initialize curl";
        return result;
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, "https://api.sarvam.ai/speech-to-text");

    CurlHeaders headers;
    std::string auth_header = "api-subscription-key: " + api_key;
    headers.reset(curl_slist_append(headers.release(), auth_header.c_str()));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

    CurlMime mime(curl_mime_init(curl.get()));
    add_mime_file(mime.get(), "file", wav_data);
    add_mime_part(mime.get(), "language_code", language);
    add_mime_part(mime.get(), "model", "saaras:v3");

    curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, mime.get());

    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        result.error_message = curl_easy_strerror(res);
        result.error_code = static_cast<int>(res);
        return result;
    }

    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        result.error_code = static_cast<int>(http_code);
        result.error_message = "HTTP " + std::to_string(http_code) + ": " + response;
        return result;
    }

    try {
        auto j = json::parse(response);
        if (j.contains("transcript")) {
            result.text = j["transcript"].get<std::string>();
            result.success = true;
        } else if (j.contains("error")) {
            result.error_message = j["error"].get<std::string>();
        } else {
            result.error_message = "Unexpected response: " + response.substr(0, 200);
        }
    } catch (...) {
        result.error_message = "Failed to parse response";
    }

    return result;
}

ApiResult ApiClient::transcribe_soniox(const std::vector<uint8_t>& wav_data,
                                        const std::string& api_key,
                                        const std::string& language) {
    ApiResult result;
    (void)language;

    if (wav_data.size() < 44 || std::memcmp(wav_data.data(), "RIFF", 4) != 0) {
        result.error_message = "Not a valid WAV file";
        return result;
    }

    auto curl = make_handle();
    if (!curl) {
        result.error_message = "Failed to initialize curl";
        return result;
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, "https://api.soniox.com/v1/files");
    auto headers = make_auth_header(api_key);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

    CurlMime mime(curl_mime_init(curl.get()));
    add_mime_file(mime.get(), "file", wav_data);
    curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, mime.get());

    std::string file_response;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &file_response);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) {
        result.error_message = std::string("Upload failed: ") + curl_easy_strerror(res);
        result.error_code = static_cast<int>(res);
        return result;
    }

    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200 && http_code != 201) {
        result.error_code = static_cast<int>(http_code);
        result.error_message = "Upload HTTP " + std::to_string(http_code) + ": " + file_response;
        return result;
    }

    std::string file_id;
    try {
        auto j = json::parse(file_response);
        file_id = j.value("id", "");
    } catch (...) {
        result.error_message = "Failed to parse file upload response";
        return result;
    }

    if (file_id.empty()) {
        result.error_message = "No file ID in upload response";
        return result;
    }

    auto curl2 = make_handle();
    if (!curl2) {
        result.error_message = "Failed to initialize curl";
        return result;
    }

    curl_easy_setopt(curl2.get(), CURLOPT_URL, "https://api.soniox.com/v1/transcriptions");
    auto headers2 = make_auth_header(api_key);
    curl_slist* sl = headers2.release();
    sl = curl_slist_append(sl, "Content-Type: application/json");
    curl_easy_setopt(curl2.get(), CURLOPT_HTTPHEADER, sl);

    json transcribe_body = {
        {"file_id", file_id},
        {"model", "stt-async-v5"}
    };
    std::string body_str = transcribe_body.dump();
    curl_easy_setopt(curl2.get(), CURLOPT_POSTFIELDS, body_str.c_str());
    curl_easy_setopt(curl2.get(), CURLOPT_POSTFIELDSIZE, body_str.size());

    std::string transcribe_response;
    curl_easy_setopt(curl2.get(), CURLOPT_WRITEDATA, &transcribe_response);

    res = curl_easy_perform(curl2.get());
    curl_slist_free_all(sl);
    if (res != CURLE_OK) {
        result.error_message = std::string("Transcription request failed: ") + curl_easy_strerror(res);
        result.error_code = static_cast<int>(res);
        return result;
    }

    curl_easy_getinfo(curl2.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200 && http_code != 201) {
        result.error_code = static_cast<int>(http_code);
        result.error_message = "Transcription HTTP " + std::to_string(http_code) + ": " + transcribe_response;
        return result;
    }

    std::string transcription_id;
    try {
        auto j = json::parse(transcribe_response);
        transcription_id = j.value("id", "");
    } catch (...) {
        result.error_message = "Failed to parse transcription response";
        return result;
    }

    if (transcription_id.empty()) {
        result.error_message = "No transcription ID";
        return result;
    }

    std::string status;
    for (int attempt = 0; attempt < 30; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        auto curl3 = make_handle();
        curl_easy_setopt(curl3.get(), CURLOPT_TIMEOUT, 10L);
        std::string status_url = "https://api.soniox.com/v1/transcriptions/" + transcription_id;
        curl_easy_setopt(curl3.get(), CURLOPT_URL, status_url.c_str());
        curl_easy_setopt(curl3.get(), CURLOPT_HTTPGET, 1L);

        auto headers3 = make_auth_header(api_key);
        curl_easy_setopt(curl3.get(), CURLOPT_HTTPHEADER, headers3.get());

        std::string status_response;
        curl_easy_setopt(curl3.get(), CURLOPT_WRITEDATA, &status_response);

        res = curl_easy_perform(curl3.get());
        if (res == CURLE_OK) {
            try {
                auto j = json::parse(status_response);
                status = j.value("status", "");
                if (status == "completed") break;
                if (status == "failed" || status == "error") {
                    result.error_message = j.value("error", "Transcription failed");
                    return result;
                }
            } catch (...) {}
        }
    }

    if (status != "completed") {
        result.error_message = "Transcription did not complete in time";
        return result;
    }

    auto curl4 = make_handle();
    std::string result_url = "https://api.soniox.com/v1/transcriptions/" + transcription_id + "/transcript";
    curl_easy_setopt(curl4.get(), CURLOPT_URL, result_url.c_str());
    curl_easy_setopt(curl4.get(), CURLOPT_HTTPGET, 1L);

    auto headers4 = make_auth_header(api_key);
    curl_easy_setopt(curl4.get(), CURLOPT_HTTPHEADER, headers4.get());

    std::string transcript_response;
    curl_easy_setopt(curl4.get(), CURLOPT_WRITEDATA, &transcript_response);

    res = curl_easy_perform(curl4.get());
    if (res != CURLE_OK) {
        result.error_message = std::string("Failed to get transcript: ") + curl_easy_strerror(res);
        return result;
    }

    try {
        auto j = json::parse(transcript_response);
        result.text = j.value("text", j.value("transcript", ""));
        result.success = !result.text.empty();
    } catch (...) {
        result.error_message = "Failed to parse transcript response";
    }

    return result;
}

double ApiClient::get_audio_duration(const std::vector<uint8_t>& wav_data) {
    if (wav_data.size() < 44) return 0.0;
    uint32_t sample_rate = static_cast<uint32_t>(wav_data[24])
        | (static_cast<uint32_t>(wav_data[25]) << 8)
        | (static_cast<uint32_t>(wav_data[26]) << 16)
        | (static_cast<uint32_t>(wav_data[27]) << 24);
    uint16_t channels = static_cast<uint16_t>(wav_data[22])
        | (static_cast<uint16_t>(wav_data[23]) << 8);
    uint16_t bps = static_cast<uint16_t>(wav_data[34])
        | (static_cast<uint16_t>(wav_data[35]) << 8);
    uint32_t data_size = static_cast<uint32_t>(wav_data[40])
        | (static_cast<uint32_t>(wav_data[41]) << 8)
        | (static_cast<uint32_t>(wav_data[42]) << 16)
        | (static_cast<uint32_t>(wav_data[43]) << 24);
    if (sample_rate == 0 || channels == 0 || bps == 0) return 0.0;
    return static_cast<double>(data_size) / (sample_rate * channels * (bps / 8.0));
}

std::string ApiClient::post_json(const std::string& url,
                                  const std::string& body,
                                  const std::string& api_key,
                                  long* http_code_out) {
    auto curl = make_handle();
    if (!curl) return "";

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, body.size());

    CurlHeaders headers;
    std::string key_hdr = "api-subscription-key: " + api_key;
    headers.reset(curl_slist_append(headers.release(), key_hdr.c_str()));
    headers.reset(curl_slist_append(headers.release(), "Content-Type: application/json"));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) return "";

    if (http_code_out) {
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, http_code_out);
    }
    return response;
}

std::string ApiClient::put_data(const std::string& url,
                                 const std::vector<uint8_t>& data,
                                 long* http_code_out) {
    CurlHandle curl(curl_easy_init());
    if (!curl) return "";

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, data.data());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, data.size());
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "VoxFlow/1.0");

    CurlHeaders headers;
    headers.reset(curl_slist_append(headers.release(), "x-ms-blob-type: BlockBlob"));
    headers.reset(curl_slist_append(headers.release(), "Content-Type: audio/wav"));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) return "";

    long http_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code_out) *http_code_out = http_code;
    if (http_code < 200 || http_code >= 300) return "";
    return response;
}

std::string ApiClient::get_url(const std::string& url,
                                const std::string& api_key) {
    auto curl = make_handle();
    if (!curl) return "";

    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);

    CurlHeaders headers;
    std::string key_hdr = "api-subscription-key: " + api_key;
    headers.reset(curl_slist_append(headers.release(), key_hdr.c_str()));
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());

    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl.get());
    if (res != CURLE_OK) return "";
    return response;
}

ApiResult ApiClient::transcribe_sarvam_batch(const std::vector<uint8_t>& wav_data,
                                              const std::string& api_key,
                                              const std::string& language) {
    ApiResult result;

    json init_body = {
        {"job_parameters", {
            {"model", "saaras:v3"},
            {"mode", "transcribe"},
            {"language_code", language}
        }}
    };

    long http_code = 0;
    std::string init_resp = post_json(
        "https://api.sarvam.ai/speech-to-text/job/v1",
        init_body.dump(), api_key, &http_code);

    if (init_resp.empty()) {
        result.error_message = "Batch init request failed";
        return result;
    }
    if (http_code != 202) {
        result.error_code = static_cast<int>(http_code);
        result.error_message = "Batch init HTTP " + std::to_string(http_code) + ": " + init_resp;
        return result;
    }

    std::string job_id;
    try {
        auto j = json::parse(init_resp);
        job_id = j.value("job_id", "");
    } catch (...) {
        result.error_message = "Failed to parse batch init response";
        return result;
    }
    if (job_id.empty()) {
        result.error_message = "No job_id in batch init response";
        return result;
    }

    json upload_req = {
        {"job_id", job_id},
        {"files", {"audio.wav"}}
    };

    std::string upload_resp = post_json(
        "https://api.sarvam.ai/speech-to-text/job/v1/upload-files",
        upload_req.dump(), api_key, &http_code);

    if (upload_resp.empty() || http_code != 200) {
        result.error_message = "Failed to get upload URLs";
        return result;
    }

    std::string upload_url;
    try {
        auto j = json::parse(upload_resp);
        auto& urls = j["upload_urls"];
        if (urls.contains("audio.wav")) {
            upload_url = urls["audio.wav"]["file_url"].get<std::string>();
        }
    } catch (...) {}

    if (upload_url.empty()) {
        result.error_message = "No upload URL in response";
        return result;
    }

    long put_code = 0;
    put_data(upload_url, wav_data, &put_code);
    if (put_code < 200 || put_code >= 300) {
        if (put_code > 0) {
            result.error_message = "Upload failed HTTP " + std::to_string(put_code);
        } else {
            result.error_message = "Upload failed: connection error";
        }
        return result;
    }

    std::string start_url = "https://api.sarvam.ai/speech-to-text/job/v1/" + job_id + "/start";
    post_json(start_url, "{}", api_key, nullptr);

    std::string status_url = "https://api.sarvam.ai/speech-to-text/job/v1/" + job_id + "/status";
    std::string job_state;
    std::string output_filename;

    for (int attempt = 0; attempt < 300; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        std::string status_resp = get_url(status_url, api_key);
        if (status_resp.empty()) continue;

        try {
            auto j = json::parse(status_resp);
            job_state = j.value("job_state", "");

            if (job_state == "Completed" || job_state == "PartiallyCompleted") {
                auto& details = j["job_details"];
                if (!details.empty() && details[0].contains("outputs")
                    && !details[0]["outputs"].empty()) {
                    output_filename = details[0]["outputs"][0].value("file_name", "");
                }
                break;
            }

            if (job_state == "Failed") {
                result.error_message = j.value("error_message", "Batch job failed");
                return result;
            }
        } catch (...) {}
    }

    if (job_state != "Completed" && job_state != "PartiallyCompleted") {
        result.error_message = "Batch job did not complete in time";
        return result;
    }

    if (output_filename.empty()) {
        result.error_message = "No output file from batch job";
        return result;
    }

    json download_req = {
        {"job_id", job_id},
        {"files", {output_filename}}
    };

    std::string download_resp = post_json(
        "https://api.sarvam.ai/speech-to-text/job/v1/download-files",
        download_req.dump(), api_key, &http_code);

    if (download_resp.empty() || http_code != 200) {
        result.error_message = "Failed to get download URLs";
        return result;
    }

    std::string download_url;
    try {
        auto j = json::parse(download_resp);
        auto& urls = j["download_urls"];
        if (urls.contains(output_filename)) {
            download_url = urls[output_filename]["file_url"].get<std::string>();
        }
    } catch (...) {}

    if (download_url.empty()) {
        result.error_message = "No download URL in response";
        return result;
    }

    auto curl = make_handle();
    if (!curl) {
        result.error_message = "Failed to init curl for download";
        return result;
    }

    curl_easy_setopt(curl.get(), CURLOPT_URL, download_url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);

    std::string transcript_resp;
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transcript_resp);

    CURLcode cres = curl_easy_perform(curl.get());
    if (cres != CURLE_OK) {
        result.error_message = std::string("Download failed: ") + curl_easy_strerror(cres);
        return result;
    }

    try {
        auto j = json::parse(transcript_resp);
        if (j.contains("transcript")) {
            result.text = j["transcript"].get<std::string>();
            result.success = !result.text.empty();
        } else if (j.contains("error")) {
            result.error_message = j["error"].get<std::string>();
        } else {
            result.error_message = "Unexpected transcript format";
        }
    } catch (...) {
        result.error_message = "Failed to parse transcript JSON";
    }

    return result;
}
