#pragma once
#include <Arduino.h>
// Stream base64 without allocating another audio-sized buffer.
class GeminiStream : public Stream {
    const String &head_, &tail_;
    const uint8_t *audio_;
    size_t bytes_, position_ = 0;
    uint8_t wav_[44] = {};
    uint8_t audioByte(size_t p) const { return p < 44 ? wav_[p] : audio_[p - 44]; }
public:
    GeminiStream(const String &head, const uint8_t *audio, size_t bytes, const String &tail)
        : head_(head), tail_(tail), audio_(audio), bytes_(bytes ? bytes + 44 : 0) {
        if (!bytes) return;
        memcpy(wav_, "RIFF", 4); memcpy(wav_ + 8, "WAVEfmt ", 8);
        memcpy(wav_ + 36, "data", 4);
        uint32_t riff = bytes + 36, format = 16, rate = 8000, byteRate = 16000, size = bytes;
        memcpy(wav_ + 4, &riff, 4); memcpy(wav_ + 16, &format, 4);
        wav_[20] = 1; wav_[22] = 1; wav_[32] = 2; wav_[34] = 16;
        memcpy(wav_ + 24, &rate, 4); memcpy(wav_ + 28, &byteRate, 4);
        memcpy(wav_ + 40, &size, 4);
    }
    size_t length() const { return head_.length() + ((bytes_ + 2) / 3) * 4 + tail_.length(); }
    int available() override { return length() - position_; }
    int peek() override {
        if (!available()) return -1;
        size_t p = position_;
        if (p < head_.length()) return head_[p];
        p -= head_.length();
        size_t encoded = ((bytes_ + 2) / 3) * 4;
        if (p >= encoded) return tail_[p - encoded];
        const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t base = (p / 4) * 3;
        unsigned slot = p % 4;
        if ((slot == 2 && base + 1 >= bytes_) || (slot == 3 && base + 2 >= bytes_)) return '=';
        uint32_t value = static_cast<uint32_t>(audioByte(base)) << 16;
        if (base + 1 < bytes_) value |= static_cast<uint32_t>(audioByte(base + 1)) << 8;
        if (base + 2 < bytes_) value |= audioByte(base + 2);
        return alphabet[(value >> (18 - slot * 6)) & 63];
    }
    int read() override { int value = peek(); if (value >= 0) ++position_; return value; }
    void flush() override {}
    size_t write(uint8_t) override { return 0; }
};
