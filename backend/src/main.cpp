#include "json_rpc.h"
#include "config.h"
#include "audio.h"
#include "clipboard.h"
#include "stream_api.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>

static std::atomic<bool> running{true};
static std::mutex cout_mutex;
static AppConfig config;
static AudioCapture capture;
static Clipboard clipboard;
static std::unique_ptr<WebSocketStream> stream;

static void write_output(const std::string& line) {
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << line;
    std::cout.flush();
}

static bool start_recording(const json& params) {
    int sr = params.value("sample_rate", config.sample_rate);
    int ch = params.value("channels", 1);

    std::string api_key;
    if (config.provider == "sarvam") {
        api_key = config.sarvam_api_key;
    } else {
        api_key = config.soniox_api_key;
    }

    if (api_key.empty()) {
        write_output(make_event("error", {
            {"message", config.provider + " API key not configured"}
        }) + "\n");
        return false;
    }

    stream = std::make_unique<WebSocketStream>();
    bool ws_ok = stream->open(config.provider, api_key, config.language, sr,
        [](const TranscriptSegment& seg) {
            json data;
            data["text"] = seg.text;
            data["is_final"] = seg.is_final;
            write_output(make_event("transcript", data) + "\n");
        });

    if (!ws_ok) {
        write_output(make_event("error", {
            {"message", "Failed to connect to " + config.provider + " WebSocket"}
        }) + "\n");
        stream.reset();
        return false;
    }

    bool ok = capture.start(sr, ch,
        [](float level) {
            write_output(make_event("level", {{"value", level}}) + "\n");
        },
        [](const int16_t* samples, size_t count) {
            if (stream && stream->is_open()) {
                stream->send_audio(samples, count);
            }
        });

    if (ok) {
        write_output(make_event("recording_started", {}) + "\n");
    } else {
        write_output(make_event("error", {
            {"message", "Failed to start audio capture"}
        }) + "\n");
        if (stream) {
            stream->close();
            stream.reset();
        }
    }
    return ok;
}

static void stop_recording(int64_t id) {
    if (!capture.is_recording()) {
        write_output(make_error(id, -1, "Not recording") + "\n");
        return;
    }

    std::vector<uint8_t> wav = capture.stop();

    write_output(make_event("processing", {}) + "\n");

    if (!stream || !stream->is_open()) {
        write_output(make_error(id, -2, "WebSocket stream not open") + "\n");
        return;
    }

    StreamResult result = stream->close();
    stream.reset();

    if (!result.success) {
        write_output(make_event("error", {
            {"message", result.error_message}
        }) + "\n");
        write_output(make_error(id, -4, result.error_message) + "\n");
        return;
    }

    std::string text = result.text;

    clipboard.copy_and_paste(text, config.append_newline);

    write_output(make_result(id, {
        {"text", text},
        {"length", text.length()}
    }) + "\n");
}

static void handle_config(const json& params) {
    config.from_json(params);
    write_output(make_event("config_updated", config.to_json()) + "\n");
}

static void process_line(const std::string& line) {
    RpcRequest req;
    if (!parse_rpc(line, req)) {
        return;
    }

    if (req.method == "start_recording") {
        start_recording(req.params);
    } else if (req.method == "stop_recording") {
        stop_recording(req.id);
    } else if (req.method == "set_config") {
        handle_config(req.params);
        if (req.id) {
            write_output(make_result(req.id, config.to_json()) + "\n");
        }
    } else if (req.method == "ping") {
        write_output(make_result(req.id, {{"pong", true}}) + "\n");
    } else if (req.method == "shutdown") {
        running = false;
    }
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    write_output(make_event("ready", {
        {"version", "2.0.0"},
        {"name", "voxflow-backend"},
        {"mode", "streaming"}
    }) + "\n");

    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        process_line(line);
    }

    if (capture.is_recording()) {
        capture.stop();
    }
    if (stream) {
        stream->close();
        stream.reset();
    }

    return 0;
}
