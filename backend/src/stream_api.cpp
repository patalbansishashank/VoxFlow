#include "stream_api.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <cctype>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <fcntl.h>
#include <sys/socket.h>

static FILE* logfile = nullptr;
static std::mutex log_mutex;
static void debug_log(const char* fmt, ...) {
    if (!logfile) {
        logfile = fopen("/tmp/voxflow-debug.log", "a");
    }
    if (logfile) {
        std::lock_guard<std::mutex> lock(log_mutex);
        va_list args;
        va_start(args, fmt);
        vfprintf(logfile, fmt, args);
        va_end(args);
        fflush(logfile);
    }
}

using json = nlohmann::json;

static size_t noop_write_cb(char* b, size_t s, size_t n, void* u) {
    (void)b; (void)s; (void)n; (void)u;
    return 0;
}

WebSocketStream::WebSocketStream() = default;

WebSocketStream::~WebSocketStream() {
    if (open_) {
        close();
    }
}

void WebSocketStream::abort() {
    if (!open_) return;
    // Flip finished_ so the ws thread exits its loop on the next poll (~10ms), without
    // sending flush or waiting for the server. Then join and clean up.
    stopping_ = true;
    finished_ = true;
    queue_cv_.notify_all();
    result_cv_.notify_all();
    if (ws_thread_.joinable()) ws_thread_.join();
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    open_ = false;
}

std::string WebSocketStream::base64_encode(const uint8_t* data, size_t len) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += (i + 1 < len) ? table[(n >> 6) & 0x3F] : '=';
        out += (i + 2 < len) ? table[n & 0x3F] : '=';
    }
    return out;
}

bool WebSocketStream::open(const std::string& provider,
                           const std::string& api_key,
                           const std::string& language,
                           int sample_rate,
                           TranscriptCallback cb,
                           const std::string& endpoint,
                           const std::string& deployment) {
    if (open_) return false;

    provider_ = provider;
    api_key_ = api_key;
    language_ = language;
    endpoint_ = endpoint;
    deployment_ = deployment;
    sample_rate_ = sample_rate;
    callback_ = std::move(cb);
    transcript_.clear();
    tail_text_.clear();
    latest_text_.clear();
    server_error_.clear();
    utterance_boundary_ = false;
    finished_ = false;
    stopping_ = false;
    flush_sent_ = false;

    curl_ = curl_easy_init();
    if (!curl_) return false;

    std::string url;
    if (provider_ == "soniox") {
        url = "wss://stt-rt.soniox.com/transcribe-websocket";
    } else if (provider_ == "azure") {
        // Azure OpenAI realtime transcription. The endpoint is https://NAME.openai.azure.com;
        // rewrite the scheme to wss:// and append the realtime transcription path. The model
        // (deployment) is sent later in the session.update config, not in the URL.
        std::string host = endpoint_;
        while (!host.empty() && host.back() == '/') host.pop_back();
        if (host.rfind("https://", 0) == 0)      host = host.substr(8);
        else if (host.rfind("http://", 0) == 0)  host = host.substr(7);
        else if (host.rfind("wss://", 0) == 0)   host = host.substr(6);
        url = "wss://" + host + "/openai/v1/realtime?intent=transcription";
    } else {
        url = "wss://api.sarvam.ai/speech-to-text/ws"
              "?model=saaras:v3"
              "&mode=transcribe"
              "&language-code=" + language_ +
              "&sample_rate=" + std::to_string(sample_rate_) +
              "&input_audio_codec=pcm_s16le"
              "&flush_signal=true";
    }

    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 600L);
    curl_easy_setopt(curl_, CURLOPT_USERAGENT, "VoxFlow/1.0");
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, noop_write_cb);

    if (provider_ == "sarvam") {
        struct curl_slist* headers = nullptr;
        std::string auth = "api-subscription-key: " + api_key_;
        headers = curl_slist_append(headers, auth.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    } else if (provider_ == "azure") {
        struct curl_slist* headers = nullptr;
        std::string auth = "api-key: " + api_key_;
        headers = curl_slist_append(headers, auth.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    }

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
        return false;
    }

    curl_socket_t sockfd;
    res = curl_easy_getinfo(curl_, CURLINFO_ACTIVESOCKET, &sockfd);
    if (res == CURLE_OK && sockfd != CURL_SOCKET_BAD) {
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    }

    open_ = true;
    ws_thread_ = std::thread(&WebSocketStream::ws_thread_fn, this);

    return true;
}

bool WebSocketStream::send_text(const std::string& msg) {
    size_t sent = 0;
    CURLcode res = curl_ws_send(curl_, msg.c_str(), msg.size(), &sent, 0, CURLWS_TEXT);
    return res == CURLE_OK;
}

bool WebSocketStream::send_binary(const void* data, size_t len) {
    size_t sent = 0;
    CURLcode res = curl_ws_send(curl_, data, len, &sent, 0, CURLWS_BINARY);
    return res == CURLE_OK;
}

bool WebSocketStream::send_empty_frame() {
    size_t sent = 0;
    CURLcode res = curl_ws_send(curl_, "", 0, &sent, 0, CURLWS_BINARY);
    return res == CURLE_OK;
}

bool WebSocketStream::send_flush() {
    if (provider_ == "soniox") {
        // Soniox end-of-stream is an EMPTY WebSocket frame (per the API docs), NOT a close
        // frame. On the empty frame the server finalizes the still-pending tail audio (a
        // realtime STT always lags the audio by a few hundred ms), streams the REMAINING
        // final tokens, and sends a `{"tokens":[],"finished":true}` response before it
        // closes. A close frame instead races that finalization and tears the socket down
        // early — dropping the last words (the "transcription cut off at the end" bug). We
        // keep the socket open and read until `finished` arrives (see ws_thread_fn).
        return send_empty_frame();
    }
    if (provider_ == "azure") {
        // Azure OpenAI end-of-stream: commit the buffered audio. With server VAD disabled
        // (turn_detection:null) the commit is what forces the model to transcribe the pending
        // audio and emit the definitive `...transcription.completed`. (Periodic commits during
        // recording keep this final chunk small so it finalizes fast.)
        json commit = {{"type", "input_audio_buffer.commit"}};
        return send_text(commit.dump());
    }
    json flush = {{"type", "flush"}};
    return send_text(flush.dump());
}

bool WebSocketStream::send_config() {
    if (provider_ == "soniox") {
        return send_soniox_config();
    }
    if (provider_ == "azure") {
        return send_azure_config();
    }
    return true;
}

bool WebSocketStream::send_azure_config() {
    // Configure a transcription-only session. turn_detection:null → we control segmentation
    // via input_audio_buffer.commit; the deployment name is the model. rate matches our
    // capture rate (no resampling — the server honours the declared rate).
    json transcription = {{"model", deployment_}, {"delay", "medium"}};
    if (!language_.empty() && language_ != "auto") {
        std::string lang = language_;
        auto dash = lang.find('-');
        if (dash != std::string::npos) lang = lang.substr(0, dash);
        transcription["language"] = lang;
    }
    json config = {
        {"type", "session.update"},
        {"session", {
            {"type", "transcription"},
            {"audio", {
                {"input", {
                    {"format", {{"type", "audio/pcm"}, {"rate", sample_rate_}}},
                    {"turn_detection", nullptr},
                    {"transcription", transcription}
                }}
            }}
        }}
    };
    return send_text(config.dump());
}

bool WebSocketStream::send_soniox_config() {
    json config = {
        {"api_key", api_key_},
        {"model", "stt-rt-v5"},
        {"audio_format", "pcm_s16le"},
        {"num_channels", 1},
        {"sample_rate", sample_rate_},
        {"enable_endpoint_detection", false}
    };

    if (!language_.empty() && language_ != "auto") {
        std::string lang = language_;
        auto dash = lang.find('-');
        if (dash != std::string::npos) {
            lang = lang.substr(0, dash);
        }
        config["language_hints"] = {lang};
        config["language_hints_strict"] = true;
    }

    return send_text(config.dump());
}

bool WebSocketStream::send_audio(const int16_t* samples, size_t count) {
    if (!open_ || stopping_) return false;

    std::vector<uint8_t> pcm(reinterpret_cast<const uint8_t*>(samples),
                             reinterpret_cast<const uint8_t*>(samples) + count * sizeof(int16_t));

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        send_queue_.push(std::move(pcm));
    }
    queue_cv_.notify_one();
    return true;
}

StreamResult WebSocketStream::close() {
    StreamResult result;

    if (!open_ || !curl_) {
        result.error_message = "Stream not open";
        return result;
    }

    stopping_ = true;
    queue_cv_.notify_one();

    bool timed_out = false;
    {
        std::unique_lock<std::mutex> lock(result_mutex_);
        // Backstop only: real transcripts finalize in ~1-3s (Sarvam streams utterance
        // results during recording, so flush finalizes fast). 5s is comfortable margin
        // for a legit slow response, yet the "no transcript" case (noise, no speech that
        // the peak-cancel didn't catch) resolves in ~5s instead of dragging on.
        timed_out = !result_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
            return finished_.load();
        });
        debug_log("[DEBUG] close() wait finished, timed_out=%d, transcript_='%s', server_error_='%s'\n",
                  timed_out, transcript_.c_str(), server_error_.c_str());
    }

    // If the server never answered, the ws thread is still spinning in its recv loop
    // (`while (!finished_)`). Force it to exit so the join below can't block forever —
    // this is what was freezing the whole backend (mic goes dead) on a stuck stream.
    if (timed_out) {
        finished_ = true;
        result_cv_.notify_all();
        queue_cv_.notify_all();
    }

    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }

    curl_easy_cleanup(curl_);
    curl_ = nullptr;
    open_ = false;

    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        // If the wait timed out before "finished" arrived, the preview (finals + tail)
        // is still better than reporting nothing.
        if (transcript_.empty() && !latest_text_.empty()) transcript_ = latest_text_;
        if (!transcript_.empty()) {
            result.success = true;
            result.text = transcript_;
        } else if (!server_error_.empty()) {
            result.error_code = -4;
            result.error_message = "Server error: " + server_error_;
        } else {
            result.error_code = -1;
            result.error_message = "No transcript received";
        }
    }

    return result;
}

void WebSocketStream::ws_thread_fn() {
    if (!send_config()) {
        finished_ = true;
        result_cv_.notify_all();
        return;
    }

    char buf[65536];
    bool sent_flush = false;
    // Sarvam/Azure post-flush finalization window (see the flush-send and CURLE_AGAIN blocks).
    std::chrono::steady_clock::time_point flush_at{};
    std::chrono::steady_clock::time_point last_flush_data{};
    bool got_flush_data = false;

    const bool is_azure = (provider_ == "azure");
    // Azure only: don't stream audio until the session.update is acknowledged (session.updated),
    // and drive periodic commits so transcription flows during recording.
    bool azure_ready = !is_azure;
    bool audio_since_commit = false;
    auto last_commit = std::chrono::steady_clock::now();

    while (!finished_) {
        if (!sent_flush) {
            size_t qsize;
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                qsize = send_queue_.size();
                // Azure holds audio in the queue until the session is configured (azure_ready);
                // nothing is lost — capture keeps buffering into send_queue_ meanwhile.
                while (azure_ready && !send_queue_.empty()) {
                    auto frame = std::move(send_queue_.front());
                    send_queue_.pop();

                    if (provider_ == "soniox") {
                        bool ok = send_binary(frame.data(), frame.size());
                        debug_log("[DEBUG] Sent binary frame %zu bytes: %s\n", frame.size(), ok ? "OK" : "FAIL");
                    } else if (is_azure) {
                        std::string b64 = base64_encode(frame.data(), frame.size());
                        json msg = {{"type", "input_audio_buffer.append"}, {"audio", b64}};
                        bool ok = send_text(msg.dump());
                        audio_since_commit = true;
                        debug_log("[DEBUG] Sent Azure frame %zu bytes: %s\n", frame.size(), ok ? "OK" : "FAIL");
                    } else {
                        std::string b64 = base64_encode(frame.data(), frame.size());
                        json msg = {
                            {"audio", {
                                {"data", b64},
                                {"sample_rate", sample_rate_},
                                {"encoding", "audio/wav"}
                            }}
                        };
                        bool ok = send_text(msg.dump());
                        debug_log("[DEBUG] Sent Sarvam frame %zu bytes: %s\n", frame.size(), ok ? "OK" : "FAIL");
                    }
                }
            }
            debug_log("[DEBUG] Drained %zu frames from queue, stopping=%d\n", qsize, stopping_.load());

            // Azure: commit accumulated audio every ~2s so the model transcribes during
            // recording (live tooltip) and the final commit on stop has only a small tail to
            // finalize — that keeps the last words from lagging behind one big buffer.
            if (is_azure && azure_ready && !stopping_ && audio_since_commit) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_commit).count() >= 2000) {
                    json commit = {{"type", "input_audio_buffer.commit"}};
                    send_text(commit.dump());
                    last_commit = now;
                    audio_since_commit = false;
                }
            }

            if (stopping_ && send_queue_.empty() && azure_ready) {
                debug_log("[DEBUG] Sending flush, transcript_='%s' latest_text_='%s'\n",
                          transcript_.c_str(), latest_text_.c_str());
                if (is_azure && !audio_since_commit) {
                    // Everything is already committed (a periodic commit just fired) — an empty
                    // commit would only earn a benign error, so skip it and let the window close
                    // on the quiet gap after the last completed.
                    sent_flush = true;
                    flush_sent_ = true;
                    flush_at = std::chrono::steady_clock::now();
                } else {
                    send_flush();
                    sent_flush = true;
                    flush_sent_ = true;
                    flush_at = std::chrono::steady_clock::now();
                    last_flush_data = flush_at;
                }
                // Do NOT finish here. The flush FORCES the server to finalize the last
                // utterance still buffered — the words spoken right before you hit stop, which
                // it hadn't segmented yet (Sarvam: one more `data`; Azure: the final
                // `...completed` after the commit). Finishing the instant we *sent* flush (the
                // old behaviour) dropped exactly those words: the "last words cut off" bug. We
                // keep reading; the post-flush window in the CURLE_AGAIN branch decides when the
                // finalized output has stopped arriving. (Soniox waits for `finished:true`.)
            }
        }

        const struct curl_ws_frame* frame = nullptr;
        size_t nread = 0;
        CURLcode res = curl_ws_recv(curl_, buf, sizeof(buf) - 1, &nread, &frame);

        if (res == CURLE_OK && nread > 0) {
            buf[nread] = '\0';
            debug_log("[DEBUG] Recv %zu bytes\n", nread);

            if (frame && (frame->flags & CURLWS_TEXT)) {
                debug_log("[DEBUG] Full Recv: %s\n", std::string(buf, nread).c_str());
                try {
                    auto j = json::parse(std::string(buf, nread));

                    if (provider_ == "soniox") {
                        if (j.contains("error_type")) {
                            {
                                std::lock_guard<std::mutex> lock(result_mutex_);
                                server_error_ = j.value("error_message", "Unknown server error");
                                // Salvage whatever we heard before the error.
                                if (!latest_text_.empty() &&
                                    latest_text_.length() > transcript_.length()) {
                                    transcript_ = latest_text_;
                                }
                            }
                            finished_ = true;
                            result_cv_.notify_all();
                            break;
                        }

                        if (j.contains("tokens")) {
                            // Soniox semantics: is_final tokens are append-only (never
                            // revised); non-final tokens are a *revisable tail* re-sent in
                            // full each message. Accumulate finals into transcript_ and
                            // replace tail_text_ wholesale — no time-based dedup. (The old
                            // start_ms watermark advanced past interim tokens and then
                            // dropped their corrected finals after flush, which is what cut
                            // off the last words of a dictation.)
                            //
                            // binds_left: segment glues onto the accumulator — it starts
                            // with whitespace, or punctuation that binds to the previous
                            // word (so no inserted space is wanted).
                            auto binds_left = [](const std::string& s) {
                                unsigned char c = static_cast<unsigned char>(s.front());
                                return std::isspace(c) ||
                                       std::strchr(".,!?;:)]}", c) != nullptr;
                            };
                            std::string tail;
                            bool tail_boundary = false;
                            for (auto& tok : j["tokens"]) {
                                if (!tok.contains("text")) continue;
                                std::string text = tok["text"].get<std::string>();
                                bool is_final = tok.value("is_final", false);

                                // Skip Soniox control tokens, but remember the utterance
                                // boundary: the next utterance's first token carries no
                                // leading space and would otherwise glue onto the previous
                                // word ("...copy." + "Hmm" -> "copy.Hmm").
                                if (text == "<end>") {
                                    utterance_boundary_ = true;
                                    tail_boundary = true;
                                    continue;
                                }
                                if (text.empty()) continue;

                                if (is_final) {
                                    std::lock_guard<std::mutex> lock(result_mutex_);
                                    if (utterance_boundary_ && !transcript_.empty() &&
                                        !binds_left(text)) {
                                        transcript_ += ' ';
                                    }
                                    transcript_ += text;
                                    utterance_boundary_ = false;
                                } else {
                                    if (tail_boundary && !tail.empty() && !binds_left(text))
                                        tail += ' ';
                                    tail += text;
                                    tail_boundary = false;
                                }
                            }

                            // Live preview = confirmed finals + current tail.
                            std::string preview;
                            {
                                std::lock_guard<std::mutex> lock(result_mutex_);
                                tail_text_ = tail;
                                preview = transcript_;
                                if (!tail.empty()) {
                                    if (!preview.empty() &&
                                        !std::isspace(static_cast<unsigned char>(
                                            preview.back())) &&
                                        !binds_left(tail)) {
                                        preview += ' ';
                                    }
                                    preview += tail;
                                }
                                latest_text_ = preview;
                            }
                            if (callback_) {
                                // Full running text, not a fragment — feeds partialText
                                // (BarWidget hover tooltip).
                                TranscriptSegment seg;
                                seg.text = preview;
                                seg.is_final = false;
                                callback_(seg);
                            }
                        }

                        if (j.contains("finished") && j["finished"].get<bool>()) {
                            debug_log("[DEBUG] Server sent finished=true\n");
                            {
                                std::lock_guard<std::mutex> lock(result_mutex_);
                                debug_log("[DEBUG] finished handler: transcript_='%s' latest_text_='%s'\n",
                                          transcript_.c_str(), latest_text_.c_str());
                                // Anything still non-final at finish (rare — the close
                                // frame finalizes everything) is better kept than lost:
                                // latest_text_ is finals + tail, exactly what we want.
                                if (!latest_text_.empty() &&
                                    latest_text_.length() > transcript_.length()) {
                                    transcript_ = latest_text_;
                                    debug_log("[DEBUG] Kept tail: transcript_ -> '%s'\n",
                                              transcript_.c_str());
                                }
                            }
                            finished_ = true;
                            result_cv_.notify_all();
                            break;
                        }
                    } else if (provider_ == "azure") {
                        std::string type = j.value("type", "");
                        if (type == "session.updated") {
                            // Config accepted — audio streaming can begin (see drain block).
                            azure_ready = true;
                        } else if (type ==
                                   "conversation.item.input_audio_transcription.delta") {
                            // Interim text for the segment currently being finalized.
                            std::string delta = j.value("delta", "");
                            if (!delta.empty()) {
                                std::string preview;
                                {
                                    std::lock_guard<std::mutex> lock(result_mutex_);
                                    tail_text_ += delta;
                                    preview = transcript_;
                                    if (!tail_text_.empty()) {
                                        if (!preview.empty() &&
                                            !std::isspace(static_cast<unsigned char>(
                                                preview.back())))
                                            preview += ' ';
                                        preview += tail_text_;
                                    }
                                    latest_text_ = preview;
                                }
                                if (callback_) {
                                    TranscriptSegment seg;
                                    seg.text = preview;
                                    seg.is_final = false;
                                    callback_(seg);
                                }
                            }
                        } else if (type ==
                                   "conversation.item.input_audio_transcription.completed") {
                            // A committed segment is finalized: append its full transcript
                            // (the interim tail is now superseded), space-joining segments.
                            std::string text = j.value("transcript", "");
                            std::string preview;
                            {
                                std::lock_guard<std::mutex> lock(result_mutex_);
                                tail_text_.clear();
                                if (!text.empty()) {
                                    unsigned char first =
                                        static_cast<unsigned char>(text.front());
                                    if (!transcript_.empty() &&
                                        !std::isspace(static_cast<unsigned char>(
                                            transcript_.back())) &&
                                        !std::isspace(first) &&
                                        std::strchr(".,!?;:)]}", first) == nullptr) {
                                        transcript_ += ' ';
                                    }
                                    transcript_ += text;
                                }
                                latest_text_ = transcript_;
                                preview = transcript_;
                            }
                            if (callback_) {
                                TranscriptSegment seg;
                                seg.text = preview;
                                seg.is_final = false;
                                callback_(seg);
                            }
                            if (flush_sent_.load()) {
                                // The finalized tail after our stop-commit — record it so the
                                // CURLE_AGAIN quiet window closes once completeds stop arriving.
                                got_flush_data = true;
                                last_flush_data = std::chrono::steady_clock::now();
                            }
                        } else if (type == "error") {
                            std::string code, emsg = "Unknown Azure error";
                            if (j.contains("error") && j["error"].is_object()) {
                                code = j["error"].value("code", "");
                                emsg = j["error"].value("message", emsg);
                            }
                            if (code == "input_audio_buffer_commit_empty") {
                                // Benign: our stop-commit had nothing new to finalize (all
                                // audio was already committed). We already hold the full text.
                                if (flush_sent_.load()) {
                                    finished_ = true;
                                    result_cv_.notify_all();
                                    break;
                                }
                                // Otherwise (shouldn't happen mid-recording) just ignore it.
                            } else {
                                {
                                    std::lock_guard<std::mutex> lock(result_mutex_);
                                    server_error_ = emsg;
                                }
                                finished_ = true;
                                result_cv_.notify_all();
                                break;
                            }
                        }
                    } else {
                        std::string type = j.value("type", "data");

                        if (type == "data" && j.contains("data")) {
                            auto& data = j["data"];
                            if (data.contains("transcript")) {
                                std::string text = data["transcript"].get<std::string>();
                                if (!text.empty()) {
                                    std::string preview;
                                    {
                                        std::lock_guard<std::mutex> lock(result_mutex_);
                                        // Sarvam sends utterance-level chunks with no
                                        // separator between them — join with a space
                                        // unless the chunk binds to the previous one.
                                        unsigned char first =
                                            static_cast<unsigned char>(text.front());
                                        if (!transcript_.empty() &&
                                            !std::isspace(static_cast<unsigned char>(
                                                transcript_.back())) &&
                                            !std::isspace(first) &&
                                            std::strchr(".,!?;:)]}", first) == nullptr) {
                                            transcript_ += ' ';
                                        }
                                        transcript_ += text;
                                        latest_text_ = transcript_;
                                        preview = transcript_;
                                    }
                                    if (callback_) {
                                        // Full running text — feeds partialText (tooltip).
                                        TranscriptSegment seg;
                                        seg.text = preview;
                                        seg.is_final = false;
                                        callback_(seg);
                                    }
                                    if (flush_sent_.load()) {
                                        // This is the flush's finalized tail. Record it and
                                        // keep the window open a beat (kFlushQuietMs) in case
                                        // the flush emits more than one segment — the
                                        // CURLE_AGAIN branch closes the window once data
                                        // stops arriving.
                                        got_flush_data = true;
                                        last_flush_data = std::chrono::steady_clock::now();
                                    }
                                }
                            }
                        } else if (type == "error") {
                            {
                                std::lock_guard<std::mutex> lock(result_mutex_);
                                if (j.contains("data") && j["data"].is_object() && j["data"].contains("message")) {
                                    server_error_ = j["data"]["message"].get<std::string>();
                                } else {
                                    server_error_ = j.value("message", "Unknown Sarvam error");
                                }
                            }
                            finished_ = true;
                            result_cv_.notify_all();
                            break;
                        }
                    }
                } catch (...) {
                }
            }
        } else if (res == CURLE_AGAIN) {
            // Sarvam post-flush finalization window: after the flush, finish once the
            // server's finalized output has stopped arriving — a short quiet gap after the
            // last post-flush `data` (kFlushQuietMs), or a hard cap (kFlushMaxMs) if the flush
            // produced nothing new because everything was already finalized while streaming.
            // The cap is deliberately generous: better a brief wait than clipping the last
            // words. `got_flush_data` gates the quiet gap so we never mistake the flush's own
            // network+processing latency for "done" and finish before the tail arrives.
            if (sent_flush && provider_ != "soniox") {
                auto now = std::chrono::steady_clock::now();
                auto since_flush = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       now - flush_at).count();
                auto since_data = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      now - last_flush_data).count();
                constexpr int kFlushQuietMs = 350;   // data settled -> finalized output done
                // safety cap (flush empty, or slow). Azure's final commit -> completed can lag
                // a touch more than Sarvam's `data`, so give it a longer net.
                const int kFlushMaxMs = is_azure ? 4000 : 1500;
                if ((got_flush_data && since_data >= kFlushQuietMs) ||
                    since_flush >= kFlushMaxMs) {
                    debug_log("[DEBUG] Sarvam flush window closed (got_data=%d, since_flush=%lldms)\n",
                              (int)got_flush_data, (long long)since_flush);
                    finished_ = true;
                    result_cv_.notify_all();
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            break;
        }
    }

    if (!finished_) {
        finished_ = true;
        result_cv_.notify_all();
    }
}
