#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>

struct ma_device;

class AudioCapture {
public:
    using LevelCallback = std::function<void(float)>;
    using FrameCallback = std::function<void(const int16_t*, size_t)>;

    AudioCapture();
    ~AudioCapture();

    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool start(int sample_rate = 16000, int channels = 1,
               LevelCallback cb = nullptr, FrameCallback frame_cb = nullptr);
    std::vector<uint8_t> stop();

    bool is_recording() const { return recording_; }

private:
    static void data_callback(ma_device* pDevice, void* pOutput, const void* pInput,
                              unsigned int frameCount);

    std::unique_ptr<ma_device> device_;
    std::vector<int16_t> buffer_;
    std::mutex mutex_;
    LevelCallback level_cb_;
    FrameCallback frame_cb_;
    bool recording_ = false;
    int sample_rate_ = 16000;
    int channels_ = 1;
};
