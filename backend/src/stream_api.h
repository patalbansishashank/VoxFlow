#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <queue>

struct TranscriptSegment {
    std::string text;
    bool is_final = false;
};

struct StreamResult {
    bool success = false;
    std::string text;
    int error_code = 0;
    std::string error_message;
};

using TranscriptCallback = std::function<void(const TranscriptSegment&)>;

class WebSocketStream {
public:
    WebSocketStream();
    ~WebSocketStream();

    WebSocketStream(const WebSocketStream&) = delete;
    WebSocketStream& operator=(const WebSocketStream&) = delete;

    bool open(const std::string& provider,
              const std::string& api_key,
              const std::string& language,
              int sample_rate,
              TranscriptCallback cb = nullptr);

    bool send_audio(const int16_t* samples, size_t count);

    StreamResult close();

    bool is_open() const { return open_.load(); }

private:
    void ws_thread_fn();
    bool send_config();
    bool send_soniox_config();
    bool send_text(const std::string& msg);
    bool send_binary(const void* data, size_t len);
    bool send_empty_frame();
    bool send_flush();

    static std::string base64_encode(const uint8_t* data, size_t len);

    void* curl_ = nullptr;
    std::thread ws_thread_;
    std::atomic<bool> open_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> flush_sent_{false};

    std::string provider_;
    std::string api_key_;
    std::string language_;
    int sample_rate_ = 16000;
    TranscriptCallback callback_;

    std::string transcript_;
    std::string latest_text_;
    std::string server_error_;
    uint64_t latest_end_ms_ = 0;
    // Soniox marks utterance boundaries with an "<end>" control token; the next
    // utterance's first token has no leading space, so naive concatenation glues
    // words ("...copy." + "Hmm" -> "copy.Hmm"). Set when we skip "<end>", consumed
    // by the next transcript_ append to insert the missing space.
    bool utterance_boundary_ = false;
    std::mutex result_mutex_;
    std::condition_variable result_cv_;
    std::atomic<bool> finished_{false};

    std::queue<std::vector<uint8_t>> send_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
};
