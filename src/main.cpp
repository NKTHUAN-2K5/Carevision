#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <driver/i2s.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 21
#define OLED_SCL 22

// MAX98357A: DIN receives audio data from the ESP32.
#define SPEAKER_BCLK 15
#define SPEAKER_LRC 16
#define SPEAKER_DIN 17

// INMP441 input bus, following Xiaozhi's simplex 32-bit I2S layout.
#define MIC_WS 4
#define MIC_SCK 5
#define MIC_SD 18
#define MIC_I2S_PORT I2S_NUM_1

// Set these with -D WIFI_SSID=... and -D WIFI_PASSWORD=... in platformio.ini.
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif
#ifndef ASR_SERVER_URL
#define ASR_SERVER_URL ""
#endif

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
WebServer server(80);
AudioGeneratorMP3 *audioMp3 = nullptr;
AudioFileSourceHTTPStream *audioFile = nullptr;
AudioOutputI2S *audioOutput = nullptr;
String pendingSpeech;
volatile bool speechPending = false;

enum class Face {
    Neutral, Happy, Sad, Angry, Surprised, Love, Thinking, Sleepy,
    Fear, Disgust, Excited, Calm, Confused, Shy, Proud, Jealous,
    Lonely, Hopeful, Grateful, Bored, Anxious, Relief, Embarrassed,
    Curious, Determined, Painful, Laughing
};
Face currentFace = Face::Neutral;
String lastMessage = "Xin chao!";
String lastReply = "Minh dang nghe ban.";
unsigned long lastBlink = 0;
bool eyesOpen = true;
unsigned long lastMicrophoneCheck = 0;

void handleChat(const String &message);

String urlEncode(const String &value) {
    const char hex[] = "0123456789ABCDEF";
    String encoded;
    for (size_t i = 0; i < value.length(); i++) {
        const uint8_t c = static_cast<uint8_t>(value[i]);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += hex[(c >> 4) & 0x0F];
            encoded += hex[c & 0x0F];
        }
    }
    return encoded;
}

String htmlEscape(const String &value) {
    String escaped;
    escaped.reserve(value.length());
    for (size_t i = 0; i < value.length(); i++) {
        switch (value[i]) {
        case '&':
            escaped += F("&amp;");
            break;
        case '<':
            escaped += F("&lt;");
            break;
        case '>':
            escaped += F("&gt;");
            break;
        case '"':
            escaped += F("&quot;");
            break;
        case '\'':
            escaped += F("&#39;");
            break;
        default:
            escaped += value[i];
            break;
        }
    }
    return escaped;
}

void stopSpeaking() {
    if (audioMp3 != nullptr) {
        // AudioGeneratorMP3::stop() releases decoder buffers, and its
        // destructor releases them again in ESP8266Audio 1.9.x.
        delete audioMp3;
        audioMp3 = nullptr;
    }
    delete audioFile;
    audioFile = nullptr;
    delete audioOutput;
    audioOutput = nullptr;
}

void speakText(const String &text) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Khong phat giong noi: WiFi chua ket noi.");
        return;
    }
    stopSpeaking();
    const String url =
        "http://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=vi&q=" +
        urlEncode(text);
    Serial.println("Dang tai am thanh TTS...");
    audioFile = new AudioFileSourceHTTPStream(url.c_str());
    audioOutput = new AudioOutputI2S();
    audioOutput->SetPinout(SPEAKER_BCLK, SPEAKER_LRC, SPEAKER_DIN);
    audioOutput->SetGain(0.65);
    audioMp3 = new AudioGeneratorMP3();
    if (!audioMp3->begin(audioFile, audioOutput)) {
        Serial.println("Khong the bat dau phat MP3 qua MAX98357A.");
        stopSpeaking();
    }
}

void testSpeaker() {
    const i2s_config_t config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    const i2s_pin_config_t pins = {
        .bck_io_num = SPEAKER_BCLK,
        .ws_io_num = SPEAKER_LRC,
        .data_out_num = SPEAKER_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK ||
        i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        Serial.println("LOA TEST LOI: khong khoi tao duoc I2S P15/P16/P17.");
        i2s_driver_uninstall(I2S_NUM_0);
        return;
    }
    int16_t samples[256 * 2];
    for (int block = 0; block < 96; block++) {
        for (int i = 0; i < 256; i++) {
            const float frequency = block < 48 ? 440.0f : 660.0f;
            const float phase = 2.0f * PI * frequency *
                                (block * 256 + i) / 16000.0f;
            const int16_t sample = static_cast<int16_t>(7000.0f * sinf(phase));
            samples[i * 2] = sample;
            samples[i * 2 + 1] = sample;
        }
        size_t written = 0;
        i2s_write(I2S_NUM_0, samples, sizeof(samples), &written, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_0);
    Serial.println("LOA TEST XONG: neu khong nghe bip, kiem tra MAX98357A/loa/nguon.");
}

void checkMicrophoneLevel() {
    int32_t samples[512];
    size_t bytesRead = 0;
    if (i2s_read(MIC_I2S_PORT, samples, sizeof(samples), &bytesRead,
                 pdMS_TO_TICKS(100)) != ESP_OK ||
        bytesRead == 0) {
        Serial.println("MIC TEST: khong doc duoc mau INMP441.");
        return;
    }
    int32_t peak = 0;
    const size_t sampleCount = bytesRead / sizeof(samples[0]);
    for (size_t i = 0; i < sampleCount; i++) {
        // INMP441 is configured for the single LEFT slot.
        const int32_t value = samples[i] < 0 ? -samples[i] : samples[i];
        if (value > peak) {
            peak = value;
        }
    }
    Serial.printf("MIC TEST: muc dinh = %ld (hay noi gan mic de thay gia tri thay doi).\n",
                  static_cast<long>(peak));
}

void transcribeMicrophone() {
    if (WiFi.status() != WL_CONNECTED || String(ASR_SERVER_URL).length() == 0) {
        Serial.println("MIC ASR: chua cau hinh ASR_SERVER_URL hoac WiFi.");
        return;
    }

    constexpr size_t durationMs = 3000;
    constexpr size_t sampleRate = 16000;
    constexpr size_t sampleCount = sampleRate * durationMs / 1000;
    constexpr size_t payloadSize = sampleCount * sizeof(int16_t);
    auto *payload = static_cast<uint8_t *>(malloc(payloadSize));
    if (payload == nullptr) {
        Serial.println("MIC ASR: khong du bo nho de ghi am.");
        return;
    }

    Serial.println("MIC ASR: dang ghi am 3 giay...");
    size_t writtenSamples = 0;
    int32_t input[256];
    while (writtenSamples < sampleCount) {
        size_t bytesRead = 0;
        if (i2s_read(MIC_I2S_PORT, input, sizeof(input), &bytesRead,
                     pdMS_TO_TICKS(200)) != ESP_OK ||
            bytesRead == 0) {
            Serial.println("MIC ASR: loi doc INMP441.");
            free(payload);
            return;
        }
        const size_t inputSamples = bytesRead / sizeof(input[0]);
        for (size_t i = 0; i < inputSamples && writtenSamples < sampleCount;
             i++) {
            const int32_t sample = input[i] >> 14;
            const int16_t clipped = static_cast<int16_t>(
                constrain(sample, -32768L, 32767L));
            memcpy(payload + writtenSamples * sizeof(clipped), &clipped,
                   sizeof(clipped));
            writtenSamples++;
        }
    }

    HTTPClient client;
    client.setTimeout(15000);
    if (!client.begin(ASR_SERVER_URL)) {
        Serial.println("MIC ASR: endpoint khong hop le.");
        free(payload);
        return;
    }
    client.addHeader("Content-Type", "audio/pcm;rate=16000");
    const int status = client.POST(payload, payloadSize);
    free(payload);
    if (status <= 0) {
        Serial.printf("MIC ASR: khong gui duoc audio (%s).\n",
                      client.errorToString(status).c_str());
        client.end();
        return;
    }
    const String response = client.getString();
    client.end();
    const int textStart = response.indexOf("\"text\":\"");
    if (textStart < 0) {
        Serial.println("MIC ASR: server khong tra ve van ban: " + response);
        return;
    }
    const int valueStart = textStart + 8;
    const int valueEnd = response.indexOf('"', valueStart);
    if (valueEnd <= valueStart) {
        Serial.println("MIC ASR: phan hoi khong hop le.");
        return;
    }
    handleChat(response.substring(valueStart, valueEnd));
}

void audioPlaybackTask(void *) {
    while (true) {
        if (speechPending) {
            const String text = pendingSpeech;
            speechPending = false;
            speakText(text);
        }
        if (audioMp3 != nullptr && !audioMp3->loop()) {
            stopSpeaking();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setupMicrophone() {
    const i2s_config_t config = {
        .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    const i2s_pin_config_t pins = {
        .bck_io_num = MIC_SCK,
        .ws_io_num = MIC_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_SD
    };
    const esp_err_t result = i2s_driver_install(MIC_I2S_PORT, &config, 0, nullptr);
    if (result != ESP_OK) {
        Serial.printf("Khong khoi tao duoc INMP441 (ma loi %d).\\n", result);
        return;
    }
    if (i2s_set_pin(MIC_I2S_PORT, &pins) != ESP_OK) {
        Serial.println("Khong gan duoc chan INMP441 P4/P5/P18.");
        i2s_driver_uninstall(MIC_I2S_PORT);
        return;
    }
    Serial.println("INMP441 da san sang (P4/P5/P18).");
}

uint8_t findOledAddress() {
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0 &&
            (address == 0x3C || address == 0x3D)) {
            return address;
        }
    }
    return 0;
}

void drawFace() {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    if (currentFace == Face::Angry || currentFace == Face::Determined) {
        display.drawLine(22, 16, 46, 22, SSD1306_WHITE);
        display.drawLine(82, 22, 106, 16, SSD1306_WHITE);
    } else if (currentFace == Face::Sad || currentFace == Face::Lonely ||
               currentFace == Face::Painful) {
        display.drawLine(22, 22, 46, 16, SSD1306_WHITE);
        display.drawLine(82, 16, 106, 22, SSD1306_WHITE);
    } else if (currentFace == Face::Happy || currentFace == Face::Love ||
               currentFace == Face::Excited || currentFace == Face::Laughing) {
        display.drawLine(22, 20, 46, 16, SSD1306_WHITE);
        display.drawLine(82, 16, 106, 20, SSD1306_WHITE);
    } else {
        display.drawLine(22, 17, 46, 17, SSD1306_WHITE);
        display.drawLine(82, 17, 106, 17, SSD1306_WHITE);
    }

    if (!eyesOpen || currentFace == Face::Sleepy) {
        display.drawLine(22, 28, 46, 28, SSD1306_WHITE);
        display.drawLine(82, 28, 106, 28, SSD1306_WHITE);
    } else if (currentFace == Face::Surprised || currentFace == Face::Fear ||
               currentFace == Face::Anxious) {
        display.drawCircle(34, 26, 9, SSD1306_WHITE);
        display.drawCircle(94, 26, 9, SSD1306_WHITE);
        display.fillCircle(34, 26, 3, SSD1306_WHITE);
        display.fillCircle(94, 26, 3, SSD1306_WHITE);
    } else if (currentFace == Face::Love) {
        display.fillTriangle(25, 23, 31, 31, 37, 23, SSD1306_WHITE);
        display.fillTriangle(85, 23, 91, 31, 97, 23, SSD1306_WHITE);
        display.fillTriangle(31, 23, 37, 31, 43, 23, SSD1306_WHITE);
        display.fillTriangle(91, 23, 97, 31, 103, 23, SSD1306_WHITE);
    } else {
        const int pupilOffset = currentFace == Face::Thinking ? 4 :
                                (currentFace == Face::Curious ? -3 : 0);
        display.fillRoundRect(22, 20, 24, 13, 5, SSD1306_WHITE);
        display.fillRoundRect(82, 20, 24, 13, 5, SSD1306_WHITE);
        display.fillCircle(34 + pupilOffset, 26, 4, SSD1306_BLACK);
        display.fillCircle(94 + pupilOffset, 26, 4, SSD1306_BLACK);
    }

    switch (currentFace) {
    case Face::Happy:
        display.drawLine(50, 43, 55, 48, SSD1306_WHITE);
        display.drawLine(55, 48, 61, 51, SSD1306_WHITE);
        display.drawLine(61, 51, 67, 51, SSD1306_WHITE);
        display.drawLine(67, 51, 73, 48, SSD1306_WHITE);
        display.drawLine(73, 48, 78, 43, SSD1306_WHITE);
        break;
    case Face::Sad:
        display.drawCircle(64, 53, 8, SSD1306_WHITE);
        display.fillRect(53, 53, 22, 10, SSD1306_BLACK);
        display.drawLine(46, 38, 48, 44, SSD1306_WHITE);
        display.drawLine(82, 38, 80, 44, SSD1306_WHITE);
        if ((millis() / 180) % 2 == 0) {
            display.drawLine(35, 34, 35, 43, SSD1306_WHITE);
            display.drawLine(93, 34, 93, 43, SSD1306_WHITE);
        } else {
            display.drawLine(35, 37, 35, 46, SSD1306_WHITE);
            display.drawLine(93, 37, 93, 46, SSD1306_WHITE);
        }
        break;
    case Face::Thinking:
        display.drawCircle(64, 47, 5, SSD1306_WHITE);
        display.drawPixel(62, 47, SSD1306_BLACK);
        display.drawPixel(66, 47, SSD1306_BLACK);
        break;
    case Face::Sleepy:
        display.drawLine(49, 48, 79, 48, SSD1306_WHITE);
        break;
    case Face::Angry:
        display.drawLine(48, 50, 57, 44, SSD1306_WHITE);
        display.drawLine(57, 44, 64, 50, SSD1306_WHITE);
        display.drawLine(64, 50, 71, 44, SSD1306_WHITE);
        display.drawLine(71, 44, 80, 50, SSD1306_WHITE);
        break;
    case Face::Surprised:
        display.drawCircle(64, 48, 7, SSD1306_WHITE);
        break;
    case Face::Love:
        display.fillTriangle(56, 45, 64, 54, 64, 40, SSD1306_WHITE);
        display.fillTriangle(72, 45, 64, 54, 64, 40, SSD1306_WHITE);
        break;
    case Face::Fear:
        display.drawCircle(64, 49, 8, SSD1306_WHITE);
        display.drawLine(48, 39, 55, 37, SSD1306_WHITE);
        display.drawLine(73, 37, 80, 39, SSD1306_WHITE);
        break;
    case Face::Disgust:
        display.drawLine(48, 47, 55, 44, SSD1306_WHITE);
        display.drawLine(55, 44, 64, 48, SSD1306_WHITE);
        display.drawLine(64, 48, 73, 44, SSD1306_WHITE);
        display.drawLine(73, 44, 80, 47, SSD1306_WHITE);
        break;
    case Face::Excited:
    case Face::Laughing:
        display.drawLine(48, 42, 55, 50, SSD1306_WHITE);
        display.drawLine(55, 50, 64, 54, SSD1306_WHITE);
        display.drawLine(64, 54, 73, 50, SSD1306_WHITE);
        display.drawLine(73, 50, 80, 42, SSD1306_WHITE);
        break;
    case Face::Calm:
    case Face::Relief:
        display.drawLine(51, 47, 59, 49, SSD1306_WHITE);
        display.drawLine(59, 49, 69, 49, SSD1306_WHITE);
        display.drawLine(69, 49, 77, 47, SSD1306_WHITE);
        break;
    case Face::Confused:
    case Face::Bored:
        display.drawLine(51, 48, 60, 45, SSD1306_WHITE);
        display.drawLine(60, 45, 69, 49, SSD1306_WHITE);
        display.drawLine(69, 49, 78, 46, SSD1306_WHITE);
        break;
    case Face::Shy:
    case Face::Embarrassed:
        display.drawCircle(47, 43, 3, SSD1306_WHITE);
        display.drawCircle(81, 43, 3, SSD1306_WHITE);
        display.drawLine(57, 48, 71, 48, SSD1306_WHITE);
        break;
    case Face::Proud:
    case Face::Hopeful:
    case Face::Grateful:
        display.drawLine(51, 47, 57, 51, SSD1306_WHITE);
        display.drawLine(57, 51, 64, 52, SSD1306_WHITE);
        display.drawLine(64, 52, 71, 51, SSD1306_WHITE);
        display.drawLine(71, 51, 77, 47, SSD1306_WHITE);
        break;
    case Face::Jealous:
        display.drawLine(50, 50, 58, 46, SSD1306_WHITE);
        display.drawLine(58, 46, 64, 50, SSD1306_WHITE);
        display.drawLine(64, 50, 70, 46, SSD1306_WHITE);
        display.drawLine(70, 46, 78, 50, SSD1306_WHITE);
        break;
    case Face::Anxious:
        display.drawLine(50, 50, 58, 46, SSD1306_WHITE);
        display.drawLine(58, 46, 64, 51, SSD1306_WHITE);
        display.drawLine(64, 51, 70, 46, SSD1306_WHITE);
        display.drawLine(70, 46, 78, 50, SSD1306_WHITE);
        break;
    case Face::Curious:
        display.drawCircle(64, 47, 4, SSD1306_WHITE);
        display.drawPixel(64, 47, SSD1306_BLACK);
        break;
    case Face::Determined:
        display.drawLine(49, 48, 79, 48, SSD1306_WHITE);
        break;
    case Face::Painful:
        display.drawLine(49, 46, 57, 51, SSD1306_WHITE);
        display.drawLine(57, 51, 64, 46, SSD1306_WHITE);
        display.drawLine(64, 46, 71, 51, SSD1306_WHITE);
        display.drawLine(71, 51, 79, 46, SSD1306_WHITE);
        break;
    case Face::Neutral:
        display.drawLine(49, 48, 79, 48, SSD1306_WHITE);
        break;
    }
    display.display();
}

void setFace(Face face) {
    currentFace = face;
    drawFace();
}

String makeReply(String message) {
    message.toLowerCase();
    message.trim();
    if (message.length() == 0) {
        return "Ban muon noi gi voi minh?";
    }
    if (message.indexOf("binh thuong") >= 0 ||
        message.indexOf("tro lai") >= 0) {
        setFace(Face::Neutral);
        return "Minh da tro lai binh thuong.";
    }
    if (message.indexOf("[cam_xuc:buon]") >= 0) {
        setFace(Face::Sad);
        return "Minh nghe ban dang buon. Ban co muon ke them khong?";
    }
    if (message.indexOf("[cam_xuc:gian]") >= 0) {
        setFace(Face::Angry);
        return "Minh nghe ban dang gian. Chung ta cung binh tinh nhe.";
    }
    if (message.indexOf("[cam_xuc:vui]") >= 0) {
        setFace(Face::Happy);
        return "Nghe giong ban vui qua! Minh cung vui voi ban.";
    }
    if (message.indexOf("[cam_xuc:batngo]") >= 0) {
        setFace(Face::Surprised);
        return "Wow, nghe that bat ngo!";
    }
    if (message.indexOf("[cam_xuc:yeu]") >= 0) {
        setFace(Face::Love);
        return "Minh cam nhan duoc tinh cam cua ban!";
    }
    if (message.indexOf("[cam_xuc:so]") >= 0 || message.indexOf("so hai") >= 0) {
        setFace(Face::Fear);
        return "Minh thay ban dang so. Minh o day voi ban.";
    }
    if (message.indexOf("[cam_xuc:kinh]") >= 0 || message.indexOf("kinh tom") >= 0) {
        setFace(Face::Disgust);
        return "Minh hieu, cam giac nay that kho chiu.";
    }
    if (message.indexOf("[cam_xuc:phan_khich]") >= 0 || message.indexOf("phan khich") >= 0) {
        setFace(Face::Excited);
        return "Tuyet voi! Minh cung rat phan khich.";
    }
    if (message.indexOf("[cam_xuc:binh_yen]") >= 0 || message.indexOf("binh yen") >= 0) {
        setFace(Face::Calm);
        return "Minh dang cam thay that binh yen.";
    }
    if (message.indexOf("[cam_xuc:boi_roi]") >= 0 || message.indexOf("boi roi") >= 0) {
        setFace(Face::Confused);
        return "Minh hoi boi roi mot chut, ban noi them nhe.";
    }
    if (message.indexOf("[cam_xuc:xau_ho]") >= 0 || message.indexOf("xau ho") >= 0) {
        setFace(Face::Shy);
        return "Minh hoi xau ho do.";
    }
    if (message.indexOf("[cam_xuc:tu_hao]") >= 0 || message.indexOf("tu hao") >= 0) {
        setFace(Face::Proud);
        return "Ban lam tot lam, hay tu hao ve minh nhe.";
    }
    if (message.indexOf("[cam_xuc:ghen]") >= 0 || message.indexOf("ghen") >= 0) {
        setFace(Face::Jealous);
        return "Minh dang hoi ghen mot chut.";
    }
    if (message.indexOf("[cam_xuc:co_don]") >= 0 || message.indexOf("co don") >= 0) {
        setFace(Face::Lonely);
        return "Minh o day, ban khong co don dau.";
    }
    if (message.indexOf("[cam_xuc:hy_vong]") >= 0 || message.indexOf("hy vong") >= 0) {
        setFace(Face::Hopeful);
        return "Minh tin dieu tot dep se den.";
    }
    if (message.indexOf("[cam_xuc:biet_on]") >= 0 || message.indexOf("biet on") >= 0) {
        setFace(Face::Grateful);
        return "Minh rat biet on ban.";
    }
    if (message.indexOf("[cam_xuc:chan]") >= 0 || message.indexOf("chan") >= 0) {
        setFace(Face::Bored);
        return "Minh dang hoi chan. Chung ta lam gi vui hon nhe.";
    }
    if (message.indexOf("[cam_xuc:lo_lang]") >= 0 || message.indexOf("lo lang") >= 0) {
        setFace(Face::Anxious);
        return "Minh hieu ban dang lo. Hay tho cham nhe.";
    }
    if (message.indexOf("[cam_xuc:nhe_nhom]") >= 0 || message.indexOf("nhe nhom") >= 0) {
        setFace(Face::Relief);
        return "That nhe nhom qua.";
    }
    if (message.indexOf("[cam_xuc:to_mo]") >= 0 || message.indexOf("to mo") >= 0) {
        setFace(Face::Curious);
        return "Minh dang rat to mo.";
    }
    if (message.indexOf("[cam_xuc:quyet_tam]") >= 0 || message.indexOf("quyet tam") >= 0) {
        setFace(Face::Determined);
        return "Minh cung ban quyet tam lam.";
    }
    if (message.indexOf("[cam_xuc:dau]") >= 0 || message.indexOf("dau qua") >= 0) {
        setFace(Face::Painful);
        return "Minh tiec vi ban dang dau.";
    }
    if (message.indexOf("[cam_xuc:cuoi]") >= 0 || message.indexOf("cuoi lon") >= 0) {
        setFace(Face::Laughing);
        return "Ha ha, vui qua!";
    }
    if (message.indexOf("vui") >= 0 || message.indexOf("cuoi") >= 0 ||
        message.indexOf("hanh phuc") >= 0 || message.indexOf("cười") >= 0) {
        setFace(Face::Happy);
        return "Minh thay ban dang vui!";
    }
    if (message.indexOf("ngac nhien") >= 0 ||
        message.indexOf("bat ngo") >= 0 || message.indexOf("wow") >= 0) {
        setFace(Face::Surprised);
        return "Minh cung bat ngo!";
    }
    if (message.indexOf("gian") >= 0 || message.indexOf("tuc") >= 0 ||
        message.indexOf("buc minh") >= 0) {
        setFace(Face::Angry);
        return "Minh thay ban dang gian. Cung binh tinh nhe.";
    }
    if (message.indexOf("yeu") >= 0 || message.indexOf("thuong") >= 0) {
        setFace(Face::Love);
        return "Minh cam nhan duoc tinh cam cua ban!";
    }
    if (message.indexOf("ngu") >= 0 || message.indexOf("buon ngu") >= 0) {
        setFace(Face::Sleepy);
        return "Minh cung thay buon ngu roi.";
    }
    if (message.indexOf("buon") >= 0 || message.indexOf("khoc") >= 0 ||
        message.indexOf("met") >= 0) {
        setFace(Face::Sad);
        return "Minh thay ban dang buon. Ban ke them cho minh nhe?";
    }
    if (message.indexOf("suy nghi") >= 0 ||
        message.indexOf("dang nghi") >= 0) {
        setFace(Face::Thinking);
        return "Minh dang suy nghi cung ban.";
    }
    if (message.indexOf("binh thuong") >= 0 ||
        message.indexOf("tro lai") >= 0) {
        setFace(Face::Neutral);
        return "Minh da tro lai binh thuong.";
    }
    if (message.indexOf("xin chao") >= 0 || message.indexOf("hello") >= 0 ||
        message.indexOf("hi") >= 0) {
        setFace(Face::Happy);
        return "Xin chao! Hom nay ban khoe khong?";
    }
    if (message.indexOf("cam on") >= 0 || message.indexOf("thank") >= 0) {
        setFace(Face::Happy);
        return "Khong co gi. Minh luon san sang giup ban.";
    }
    if (message.indexOf("yeu") >= 0 || message.indexOf("thich") >= 0) {
        setFace(Face::Love);
        return "Minh cung rat quy ban!";
    }
    if (message.indexOf("gian") >= 0 || message.indexOf("tuc") >= 0) {
        setFace(Face::Angry);
        return "Minh hieu. Chung ta cung binh tinh nhe.";
    }
    if (message.indexOf("wow") >= 0 || message.indexOf("ngac nhien") >= 0) {
        setFace(Face::Surprised);
        return "Wow! That la bat ngo!";
    }
    if (message.indexOf("buon") >= 0 || message.indexOf("met") >= 0) {
        setFace(Face::Sad);
        return "Minh tiec vi ban dang khong vui. Hay nghi ngoi mot chut nhe.";
    }
    if (message.indexOf("ban la ai") >= 0 || message.indexOf("ten") >= 0) {
        setFace(Face::Happy);
        return "Minh la ban dong hanh ESP32 cua ban.";
    }
    setFace(Face::Thinking);
    return "Minh da nghe: \"" + message + "\". Ban ke them cho minh nhe?";
}

void handleChat(const String &message) {
    lastMessage = message;
    lastReply = makeReply(message);
    Serial.println("Ban: " + lastMessage);
    Serial.println("ESP32: " + lastReply);
    pendingSpeech = lastReply;
    speechPending = true;
}

void handleRoot() {
    server.send(200, "text/html",
                "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width,initial-scale=1\">"
                "<title>ESP32 Chat</title><style>body{font:18px sans-serif;max-width:620px;margin:30px auto;padding:0 15px}"
                "input{font-size:18px;width:70%;padding:8px}button{font-size:18px;padding:8px;margin:4px}"
                "#status{color:#555}</style><h2>ESP32 OLED Chat</h2>"
                "<p id=status>Nhan nut mic, cho phep microphone, roi noi tieng Viet.</p>"
                "<form id=form><input id=text name=message autofocus placeholder=\"Nhap tin nhan\">"
                "<button>Gui</button><button type=button id=mic>Mic</button></form>"
                "<p>Ban: " + htmlEscape(lastMessage) + "</p><p>ESP32: " +
                    htmlEscape(lastReply) + "</p>"
                "<script>"
                "const form=document.getElementById('form'),text=document.getElementById('text'),status=document.getElementById('status');"
                "const SpeechRecognition=window.SpeechRecognition||window.webkitSpeechRecognition;"
                "function plain(s){return s.toLowerCase().normalize('NFD').replace(/[\\u0300-\\u036f]/g,'').replace(/đ/g,'d');}"
                "function emotion(s){s=plain(s);"
                "if(/buon|khoc|met|that vong/i.test(s))return 'buon';"
                "if(/gian|tuc|buc minh|kho chiu/i.test(s))return 'gian';"
                "if(/vui|cuoi|cuoi tuoi|hanh phuc|tuyet voi/i.test(s))return 'vui';"
                "if(/wow|ngac nhien|bat ngo/i.test(s))return 'batngo';"
                "if(/yeu|thuong|nho/i.test(s))return 'yeu';"
                "if(/so hai|so/i.test(s))return 'so';"
                "if(/kinh tom|ghe/i.test(s))return 'kinh';"
                "if(/phan khich|hao hung/i.test(s))return 'phan_khich';"
                "if(/binh yen|thu gian/i.test(s))return 'binh_yen';"
                "if(/boi roi|khong hieu/i.test(s))return 'boi_roi';"
                "if(/xau ho|ngai/i.test(s))return 'xau_ho';"
                "if(/tu hao/i.test(s))return 'tu_hao';"
                "if(/ghen/i.test(s))return 'ghen';"
                "if(/co don|mot minh/i.test(s))return 'co_don';"
                "if(/hy vong|tin tuong/i.test(s))return 'hy_vong';"
                "if(/biet on|cam kich/i.test(s))return 'biet_on';"
                "if(/chan/i.test(s))return 'chan';"
                "if(/lo lang|bat an/i.test(s))return 'lo_lang';"
                "if(/nhe nhom|tho phao/i.test(s))return 'nhe_nhom';"
                "if(/to mo|muon biet/i.test(s))return 'to_mo';"
                "if(/quyet tam|co gang/i.test(s))return 'quyet_tam';"
                "if(/dau qua|dau/i.test(s))return 'dau';"
                "if(/cuoi lon|ha ha/i.test(s))return 'cuoi';return '';}"
                "function send(s){const e=emotion(s);text.value=e?'[cam_xuc:'+e+'] '+s:s;form.requestSubmit();}"
                "form.addEventListener('submit',e=>{e.preventDefault();if(text.value.trim())location.href='/chat?message='+encodeURIComponent(text.value);});"
                "document.getElementById('mic').onclick=()=>{"
                "if(!SpeechRecognition){status.textContent='Trinh duyet khong ho tro nhan giong noi. Hay dung Chrome/Edge.';return;}"
                "const r=new SpeechRecognition();r.lang='vi-VN';r.interimResults=false;r.maxAlternatives=1;"
                "r.onstart=()=>status.textContent='Dang nghe...';"
                "r.onerror=e=>{if(e.error==='network')"
                "status.textContent='Nhan dang giong noi bi loi mang. Hay kiem tra Internet hoac dung Chrome/Edge chinh thuc.';"
                "else if(e.error==='not-allowed'||e.error==='service-not-allowed')"
                "status.textContent='Trinh duyet dang chan microphone. Hay bam bieu tuong khoa/cai dat ben trai dia chi, cho phep Microphone, roi tai lai trang.';"
                "else status.textContent='Khong nghe duoc: '+e.error;}"
                "r.onresult=e=>{const s=e.results[0][0].transcript;status.textContent='Da nghe: '+s;send(s);};"
                "r.onend=()=>{if(status.textContent==='Dang nghe...')status.textContent='Khong nhan duoc cau noi. Hay noi ngay sau khi bam Mic.';};"
                "setTimeout(()=>{if(status.textContent==='Dang nghe...'){r.stop();status.textContent='Het thoi gian nghe. Hay bam Mic va noi ngay.';}},12000);"
                "r.start();};"
                "</script>");
}

void handleChatRequest() {
    if (!server.hasArg("message")) {
        server.send(400, "text/plain; charset=utf-8", "Missing message");
        return;
    }
    const String message = server.arg("message");
    if (message.length() > 256) {
        server.send(413, "text/plain; charset=utf-8",
                    "Message is too long (maximum 256 characters).");
        return;
    }
    handleChat(message);
    server.sendHeader("Location", "/");
    server.send(303);
}

void handleListenRequest() {
    transcribeMicrophone();
    server.sendHeader("Location", "/");
    server.send(303);
}

void connectWiFi() {
    if (String(WIFI_SSID) == "YOUR_WIFI_SSID") {
        Serial.println("WiFi chua duoc cau hinh. Dat WIFI_SSID/WIFI_PASSWORD trong platformio.ini.");
        setFace(Face::Sleepy);
        return;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Dang ket noi WiFi");
    const unsigned long deadline = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
        delay(300);
        Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi OK. Mo http://");
        Serial.println(WiFi.localIP());
        setFace(Face::Happy);
        server.on("/", handleRoot);
        server.on("/chat", handleChatRequest);
        server.on("/listen", handleListenRequest);
        server.begin();
    } else {
        Serial.println("WiFi ket noi that bai; van co the chat qua Serial.");
        setFace(Face::Sad);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    audioLogger = &Serial;
    Wire.begin(OLED_SDA, OLED_SCL);
    const uint8_t oledAddress = findOledAddress();
    if (oledAddress == 0 ||
        !display.begin(SSD1306_SWITCHCAPVCC, oledAddress)) {
        Serial.println("OLED NOT FOUND/INIT FAILED.");
        while (true) {
            delay(1000);
        }
    }
    setFace(Face::Neutral);
    testSpeaker();
    setupMicrophone();
    connectWiFi();
    speakText("Xin chao! Toi la robot Carevidssio.");
    xTaskCreatePinnedToCore(audioPlaybackTask, "audioPlayback", 4096, nullptr,
                            1, nullptr, 1);
    Serial.println("Nhap tin nhan trong Serial Monitor va nhan Enter.");
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        server.handleClient();
    }
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        if (input.equalsIgnoreCase("mic")) {
            transcribeMicrophone();
        } else {
            handleChat(input);
        }
    }
    if (millis() - lastMicrophoneCheck > 2000) {
        lastMicrophoneCheck = millis();
        checkMicrophoneLevel();
    }
    if (millis() - lastBlink > 4500) {
        lastBlink = millis();
        eyesOpen = false;
        drawFace();
        delay(120);
        eyesOpen = true;
        drawFace();
    }
}
