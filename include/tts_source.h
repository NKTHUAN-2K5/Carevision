#pragma once
#include <AudioFileSource.h>
#include <HTTPClient.h>

// ponytail: bounded HTTP MP3 stream; use authenticated HTTPS TTS for production.
class TtsSource : public AudioFileSource {
    WiFiClient client_;
    HTTPClient http_;
    uint32_t started_, position_ = 0, size_ = 0;
    bool opened_ = false;
    bool knownSize_ = false;
public:
    explicit TtsSource(const char *url) : started_(millis()) {
        http_.setConnectTimeout(2500);
        http_.setTimeout(2500);
        http_.useHTTP10(true); // EOF-delimited body, no chunk framing in MP3 bytes.
        if (!http_.begin(client_, url)) return;
        const char *headers[] = {"Content-Type"};
        http_.collectHeaders(headers, 1);
        int status = http_.GET();
        int size = http_.getSize();
        if (status != 200 || size == 0 || size > 65536 ||
            !http_.header("Content-Type").startsWith("audio/")) {
            Serial.printf("TTS: HTTP %d, bytes %d; bo qua am thanh\n", status, size);
            http_.end(); return;
        }
        knownSize_ = size > 0;
        size_ = knownSize_ ? size : 65536;
        auto *stream = http_.getStreamPtr();
        if (!stream) { http_.end(); return; }
        stream->setTimeout(200);
        opened_ = true;
        Serial.printf("TTS: da mo %u byte\n", size_);
    }
    ~TtsSource() override { close(); }
    uint32_t read(void *data, uint32_t len) override {
        if (!data || !len || !opened_) return 0;
        if (millis() - started_ >= 20000 || position_ >= size_) { close(); return 0; }
        if (len > size_ - position_) len = size_ - position_;
        auto *stream = http_.getStreamPtr();
        if (!stream) { close(); return 0; }
        size_t count = stream->readBytes(static_cast<uint8_t *>(data), len);
        position_ += count;
        if (!count) close();
        return count;
    }
    bool close() override { http_.end(); opened_ = false; return true; }
    bool isOpen() override { return opened_; }
    uint32_t getSize() override { return knownSize_ ? size_ : 0; }
    uint32_t getPos() override { return position_; }
};
