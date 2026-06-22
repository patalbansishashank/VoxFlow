#include "audio.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <cmath>
#include <cstring>

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    if (recording_) {
        stop();
    }
}

bool AudioCapture::start(int sample_rate, int channels, LevelCallback cb, FrameCallback frame_cb) {
    if (recording_) return false;

    level_cb_ = std::move(cb);
    frame_cb_ = std::move(frame_cb);
    buffer_.clear();
    recording_ = true;
    sample_rate_ = sample_rate;
    channels_ = channels;

    device_ = std::make_unique<ma_device>();

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_s16;
    config.capture.channels = channels;
    config.sampleRate = sample_rate;
    config.dataCallback = data_callback;
    config.pUserData = this;

    ma_result result = ma_device_init(nullptr, &config, device_.get());
    if (result != MA_SUCCESS) {
        recording_ = false;
        device_.reset();
        return false;
    }

    result = ma_device_start(device_.get());
    if (result != MA_SUCCESS) {
        ma_device_uninit(device_.get());
        device_.reset();
        recording_ = false;
        return false;
    }

    return true;
}

std::vector<uint8_t> AudioCapture::stop() {
    if (!recording_) return {};

    recording_ = false;

    if (device_) {
        ma_device_stop(device_.get());
        ma_device_uninit(device_.get());
        device_.reset();
    }

    std::vector<int16_t> samples;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples = std::move(buffer_);
        buffer_.clear();
    }

    if (samples.empty()) return {};

    uint32_t num_samples = static_cast<uint32_t>(samples.size());
    uint32_t data_size = num_samples * sizeof(int16_t);
    uint32_t file_size = 36 + data_size;

    std::vector<uint8_t> wav(file_size + 8);
    uint32_t pos = 0;

    auto write_u32 = [&](uint32_t v) {
        wav[pos++] = v & 0xff;
        wav[pos++] = (v >> 8) & 0xff;
        wav[pos++] = (v >> 16) & 0xff;
        wav[pos++] = (v >> 24) & 0xff;
    };

    auto write_u16 = [&](uint16_t v) {
        wav[pos++] = v & 0xff;
        wav[pos++] = (v >> 8) & 0xff;
    };

    auto write_buf = [&](const void* src, uint32_t len) {
        std::memcpy(&wav[pos], src, len);
        pos += len;
    };

    std::memcpy(&wav[pos], "RIFF", 4); pos += 4;
    write_u32(file_size);
    std::memcpy(&wav[pos], "WAVE", 4); pos += 4;

    std::memcpy(&wav[pos], "fmt ", 4); pos += 4;
    write_u32(16);
    write_u16(1);
    write_u16(static_cast<uint16_t>(channels_));
    write_u32(static_cast<uint32_t>(sample_rate_));
    write_u32(static_cast<uint32_t>(sample_rate_ * channels_ * sizeof(int16_t)));
    write_u16(static_cast<uint16_t>(channels_ * sizeof(int16_t)));
    write_u16(16);

    std::memcpy(&wav[pos], "data", 4); pos += 4;
    write_u32(data_size);
    write_buf(samples.data(), data_size);

    return wav;
}

void AudioCapture::data_callback(ma_device* pDevice, void* pOutput, const void* pInput,
                                  unsigned int frameCount) {
    (void)pOutput;

    auto* self = static_cast<AudioCapture*>(pDevice->pUserData);
    if (!self || !pInput) return;

    unsigned int sampleCount = frameCount * pDevice->capture.channels;
    auto* samples = static_cast<const int16_t*>(pInput);

    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->buffer_.insert(self->buffer_.end(), samples, samples + sampleCount);
    }

    if (self->frame_cb_) {
        self->frame_cb_(samples, sampleCount);
    }

    if (self->level_cb_) {
        float rms = 0.0f;
        for (unsigned int i = 0; i < sampleCount; ++i) {
            float s = samples[i] / 32768.0f;
            rms += s * s;
        }
        rms = std::sqrt(rms / static_cast<float>(sampleCount));
        self->level_cb_(rms);
    }
}
