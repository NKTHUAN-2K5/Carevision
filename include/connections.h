#pragma once
#include "voice_activity.h"

String jsonText(cJSON *object, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(v) ? String(v->valuestring) : String();
}
void sendJson(int status, cJSON *object) {
    char *body = cJSON_PrintUnformatted(object);
    server.send(status, "application/json; charset=utf-8", body ? body : "{}");
    cJSON_free(body); cJSON_Delete(object);
}
void apiError(int status, const String &error) {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "error", error.c_str()); sendJson(status, body);
}
void apiOk(const String &message = "OK") {
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "message", message.c_str()); sendJson(200, body);
}
void queueSpeech(const String &text) {
    xSemaphoreTake(speechMutex, portMAX_DELAY);
    pendingSpeech = text; speechPending = true;
    xSemaphoreGive(speechMutex);
}
bool askGemini(const String &text, const uint8_t *pcm, size_t pcmSize) {
    lastError = "";
    if (WiFi.status() != WL_CONNECTED || geminiKey.isEmpty()) {
        lastError = "Can WiFi va Gemini API key"; return false;
    }
    if (setupOpen()) { lastError = "Dong che do cau hinh BLE (cho het 2 phut) truoc khi goi AI"; return false; }
    if (time(nullptr) < 1700000000) {
        lastError = "Chua dong bo gio NTP. Thu lai sau vai giay"; return false;
    }
    cJSON *prompt = cJSON_CreateString((String(
        "Ban la Carevision. Tra loi bang tieng Viet co dau, duoi 180 ky tu. "
        "Mo dau [cam_xuc:vui], [cam_xuc:buon] hoac [cam_xuc:to_mo]. "
        "Neu co am thanh, nghe va tra loi cau noi do. Neu khong co loi noi ro rang, chi tra SILENCE. ") + text).c_str());
    char *quoted = cJSON_PrintUnformatted(prompt); cJSON_Delete(prompt);
    if (!quoted) { lastError = "Khong du RAM"; return false; }
    String head = "{\"contents\":[{\"parts\":[{\"text\":";
    head += quoted; cJSON_free(quoted); head += "}";
    String tail;
    if (pcmSize) {
        head += ",{\"inline_data\":{\"mime_type\":\"audio/wav\",\"data\":\"";
        tail = "\"}}";
    }
    tail += "]}],\"generationConfig\":{\"maxOutputTokens\":512}}";
    WiFiClientSecure tls; tls.setCACert(GOOGLE_CA); tls.setHandshakeTimeout(15);
    HTTPClient client; client.setTimeout(35000); client.setConnectTimeout(15000);
    if (!client.begin(tls, "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent")) {
        lastError = "Khong mo duoc HTTPS"; return false;
    }
    client.addHeader("Content-Type", "application/json");
    client.addHeader("x-goog-api-key", geminiKey);
    GeminiStream stream(head, pcm, pcmSize, tail);
    int status = client.sendRequest("POST", &stream, stream.length());
    if (status != 200) {
        lastError = "Gemini HTTP " + String(status) + ". Kiem tra key, mang va han muc.";
        if (status < 0) {
            char reason[160] = {};
            tls.lastError(reason, sizeof(reason));
            Serial.printf("Gemini TLS: %s\n", reason);
        }
        client.end(); return false;
    }
    if (client.getSize() > 16384) { client.end(); lastError = "Phan hoi AI qua lon"; return false; }
    String response = client.getString(); client.end();
    cJSON *body = cJSON_Parse(response.c_str());
    cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(body, "candidates"), 0);
    String finishReason = jsonText(first, "finishReason");
    cJSON *parts = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(first, "content"), "parts");
    String reply;
    cJSON *part;
    cJSON_ArrayForEach(part, parts) {
        if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(part, "thought"))) reply += jsonText(part, "text");
    }
    cJSON_Delete(body); reply.trim();
    if (finishReason != "STOP") { lastError = "AI tra loi chua hoan tat: " + finishReason; return false; }
    if (reply.isEmpty() || reply.length() > 1024) { lastError = "AI khong tra loi hop le"; return false; }
    int bracket = reply.indexOf(']');
    String spoken = reply.startsWith("[cam_xuc:") && bracket > 0 ? reply.substring(bracket + 1) : reply;
    spoken.trim();
    if (spoken == "SILENCE" || spoken == "SILENCE.") {
        Serial.println("MIC: khong nhan ra loi noi, quay lai cho nghe");
        return true;
    }
    makeReply(reply);
    lastMessage = pcmSize ? "[Giong noi tu INMP441]" : text;
    lastReply = spoken;
    Serial.println("AI: " + lastReply);
    queueSpeech(lastReply); return true;
}
bool enqueueWifi(const String &ssid, const String &password) {
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
        (!password.isEmpty() && password.length() < 8)) return false;
    WifiCommand request = {};
    ssid.toCharArray(request.ssid, sizeof(request.ssid));
    password.toCharArray(request.password, sizeof(request.password));
    return configQueue && xQueueSend(configQueue, &request, 0) == pdTRUE;
}
class ConnectionSecurity : public BLESecurityCallbacks {
    uint32_t passkey_;
public:
    explicit ConnectionSecurity(uint32_t passkey) : passkey_(passkey) {}
    uint32_t onPassKeyRequest() override { return passkey_; }
    void onPassKeyNotify(uint32_t) override {}
    bool onConfirmPIN(uint32_t) override { return setupOpen(); }
    bool onSecurityRequest() override { return setupOpen(); }
    void onAuthenticationComplete(esp_ble_auth_cmpl_t) override {}
};
class ConfigCharacteristic : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override {
        if (!setupOpen()) return;
        std::string raw = characteristic->getValue();
        if (raw.empty() || raw.size() > 480) return;
        cJSON *body = cJSON_Parse(raw.c_str());
        if (jsonText(body, "action") == "wifi") enqueueWifi(jsonText(body, "ssid"), jsonText(body, "password"));
        else if (jsonText(body, "action") == "stop") {
            xSemaphoreTake(speechMutex, portMAX_DELAY);
            speechPending = false; stopSpeechRequested = true;
            xSemaphoreGive(speechMutex);
        }
        cJSON_Delete(body);
    }
};
void openSetup() {
    setupUntil = millis() + 120000;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("Carevision-Setup", setupPassword.c_str(), 1, false, 1);
    Serial.println("Setup 120s: http://192.168.4.1 ; password: " + setupPassword);
    if (!bleStarted) {
        uint32_t passkey = 100000 + esp_random() % 900000;
        BLEDevice::init("Carevision"); BLEDevice::setMTU(512);
        BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
        BLEDevice::setSecurityCallbacks(new ConnectionSecurity(passkey));
        auto *security = new BLESecurity();
        security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM);
        security->setCapability(ESP_IO_CAP_OUT); security->setStaticPIN(passkey);
        security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
        auto *service = BLEDevice::createServer()->createService("7cf10000-6c49-4e33-9331-cc32a5d99100");
        auto *characteristic = service->createCharacteristic("7cf10001-6c49-4e33-9331-cc32a5d99100", BLECharacteristic::PROPERTY_WRITE);
        characteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
        characteristic->setCallbacks(new ConfigCharacteristic()); service->start();
        BLEDevice::getAdvertising()->addServiceUUID(service->getUUID()); bleStarted = true;
        Serial.printf("BLE pairing PIN: %06u\n", passkey);
    }
    BLEDevice::startAdvertising();
}
void setupConnections() {
    pinMode(0, INPUT_PULLUP); preferences.begin("carevision", false);
    savedSsid = preferences.getString("ssid", WIFI_SSID);
    savedPassword = preferences.getString("password", WIFI_PASSWORD);
    geminiKey = preferences.getString("gemini", "");
    asrEndpoint = preferences.getString("endpoint", ASR_SERVER_URL);
    setupPassword = preferences.getString("setup-pass", "");
    if (setupPassword.isEmpty()) {
        setupPassword = String(esp_random(), HEX) + String(esp_random(), HEX);
        preferences.putString("setup-pass", setupPassword);
    }
    csrfToken = String(esp_random(), HEX) + String(esp_random(), HEX);
    configQueue = xQueueCreate(1, sizeof(WifiCommand));
    WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);
    if (savedSsid != "YOUR_WIFI_SSID") WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
    configTime(0, 0, "time.cloudflare.com", "pool.ntp.org", "time.google.com");
    const char *headers[] = {"X-Carevision-Token"}; server.collectHeaders(headers, 1);
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", WEB_UI); });
    server.on("/api/status", HTTP_GET, [] {
        cJSON *body = cJSON_CreateObject();
        cJSON_AddBoolToObject(body, "connected", WiFi.status() == WL_CONNECTED);
        cJSON_AddStringToObject(body, "ssid", WiFi.SSID().c_str());
        cJSON_AddStringToObject(body, "ip", WiFi.localIP().toString().c_str());
        cJSON_AddNumberToObject(body, "rssi", WiFi.RSSI());
        cJSON_AddNumberToObject(body, "freeHeap", ESP.getFreeHeap());
        cJSON_AddBoolToObject(body, "micReady", microphoneReady);
        cJSON_AddBoolToObject(body, "keyConfigured", !geminiKey.isEmpty());
        cJSON_AddBoolToObject(body, "autoListen", autoListen);
        xSemaphoreTake(speechMutex, portMAX_DELAY);
        bool speaking = audioBusy || speechPending;
        cJSON_AddStringToObject(body, "speakerStatus", speakerStatus.c_str());
        cJSON_AddNumberToObject(body, "speakerBytes", speakerBytes);
        xSemaphoreGive(speechMutex);
        const char *voiceState = !autoListen ? "paused" : setupOpen() ? "setup" :
            WiFi.status() != WL_CONNECTED ? "offline" : geminiKey.isEmpty() ? "missing-key" :
            speaking ? "speaking" : voiceRecording ? "recording" : "listening";
        cJSON_AddStringToObject(body, "voiceState", voiceState);
        cJSON_AddBoolToObject(body, "setupOpen", setupOpen());
        cJSON_AddBoolToObject(body, "bleEnabled", bleStarted && setupOpen());
        cJSON_AddStringToObject(body, "endpoint", asrEndpoint.c_str());
        cJSON_AddStringToObject(body, "message", lastMessage.c_str());
        cJSON_AddStringToObject(body, "reply", lastReply.c_str());
        cJSON_AddStringToObject(body, "error", lastError.c_str());
        cJSON_AddStringToObject(body, "token", csrfToken.c_str());
        server.sendHeader("Cache-Control", "no-store"); sendJson(200, body);
    });
    server.on("/api/scan", HTTP_GET, [] {
        cJSON *body = cJSON_CreateObject(); cJSON *networks = cJSON_AddArrayToObject(body, "networks");
        int count = WiFi.scanNetworks();
        for (int i = 0; i < count && i < 30; ++i) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "ssid", WiFi.SSID(i).c_str());
            cJSON_AddNumberToObject(item, "rssi", WiFi.RSSI(i)); cJSON_AddItemToArray(networks, item);
        }
        WiFi.scanDelete(); sendJson(200, body);
    });
    server.onNotFound([] {
        String path = server.uri();
        if (server.method() != HTTP_POST || !path.startsWith("/api/")) { apiError(404, "Not found"); return; }
        if (server.header("X-Carevision-Token") != csrfToken) { apiError(403, "Tai lai trang de lay phien moi"); return; }
        String raw = server.arg("plain");
        if (raw.length() > 2048) { apiError(413, "Du lieu qua dai"); return; }
        cJSON *body = cJSON_Parse(raw.c_str());
        if (!cJSON_IsObject(body)) { cJSON_Delete(body); apiError(400, "JSON khong hop le"); return; }
        if (path == "/api/auto-listen") {
            autoListen = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(body, "enabled"));
            apiOk(autoListen ? "Da bat nghe tu dong" : "Da tat nghe tu dong");
        } else if (path == "/api/chat") {
            String message = jsonText(body, "message"); message.trim();
            if (message.isEmpty() || message.length() > 1024) apiError(400, "Tin nhan rong hoac qua dai");
            else if (askGemini(message)) apiOk(); else apiError(502, lastError);
        } else if (path == "/api/listen") {
            lastError = ""; transcribeMicrophone();
            if (lastError.isEmpty()) apiOk(); else apiError(502, lastError);
        } else if (path == "/api/stop") {
            xSemaphoreTake(speechMutex, portMAX_DELAY); speechPending = false; stopSpeechRequested = true;
            xSemaphoreGive(speechMutex); apiOk();
        } else if (path == "/api/close-setup") {
            apiOk("Dong cau hinh, khoi dong lai mach");
            delay(200); ESP.restart();
        } else if (path == "/api/wifi") {
            if (!setupOpen()) apiError(403, "Giu BOOT 2 giay de mo cau hinh");
            else if (wifiChanging || !enqueueWifi(jsonText(body, "ssid"), jsonText(body, "password"))) apiError(400, "WiFi khong hop le hoac dang ban");
            else apiOk("Dang thu WiFi");
        } else if (path == "/api/config") {
            if (!setupOpen()) apiError(403, "Giu BOOT 2 giay de mo cau hinh");
            else {
                String key = jsonText(body, "key"), endpoint = jsonText(body, "endpoint");
                if (key.length() > 256 || key.indexOf('\r') >= 0 || key.indexOf('\n') >= 0 || endpoint.length() > 256 ||
                    (!endpoint.isEmpty() && !endpoint.startsWith("http://") && !endpoint.startsWith("https://"))) apiError(400, "Cau hinh khong hop le");
                else if (!key.isEmpty() && server.client().localIP() != WiFi.softAPIP()) apiError(403, "Chi nhap key qua mang Carevision-Setup rieng");
                else {
                    if (!key.isEmpty()) { preferences.putString("gemini", key); geminiKey = key; }
                    if (!endpoint.isEmpty()) { preferences.putString("endpoint", endpoint); asrEndpoint = endpoint; }
                    apiOk("Da luu");
                }
            }
        } else if (path == "/api/health") {
            if (askGemini("Chi tra loi: Ket noi AI thanh cong")) apiOk("Gemini da tra loi truc tiep tu ESP32");
            else apiError(502, lastError);
        } else if (path == "/api/test") {
            String action = jsonText(body, "action");
            if (action == "mic") { checkMicrophoneLevel(); apiOk("Muc mic in tren Serial Monitor"); }
            else if (action == "speaker") { queueSpeech("Xin chao, day la kiem tra loa Carevision."); apiOk("Da gui am thanh thu"); }
            else apiError(400, "Lenh khong hop le");
        } else apiError(404, "Not found");
        cJSON_Delete(body);
    });
    server.begin();
}
void pollVoice() {
    static VoiceActivity vad;
    static int16_t *recording = nullptr;
    static size_t count = 0;
    static uint32_t cooldown = 0;
    bool speaking;
    xSemaphoreTake(speechMutex, portMAX_DELAY);
    speaking = audioBusy || speechPending;
    xSemaphoreGive(speechMutex);
    if (!autoListen || !microphoneReady || speaking || setupOpen() || geminiKey.isEmpty() || WiFi.status() != WL_CONNECTED) {
        free(recording); recording = nullptr; count = 0; voiceRecording = false; vad.reset(); cooldown = millis() + 300; return;
    }
    int32_t input[256]; size_t bytes = 0;
    if (i2s_read(MIC_I2S_PORT, input, sizeof(input), &bytes, 0) != ESP_OK || bytes == 0) return;
    if (static_cast<int32_t>(millis() - cooldown) < 0) return;
    int16_t frame[64]; unsigned sum = 0; size_t samples = 0;
    // LEFT slot is fixed: INMP441 L/R must be tied to GND. No per-sample channel switching.
    for (size_t i = 0; i + 3 < bytes / 4; i += 4) {
        int32_t value = input[i] >> 16;
        frame[samples++] = value;
        sum += value < 0 ? -value : value;
    }
    if (!samples) return;
    if (vad.update(sum / samples)) {
        recording = static_cast<int16_t *>(malloc(48000)); count = 0;
        if (!recording) { lastError = "Khong du RAM VAD"; vad.reset(); cooldown = millis() + 10000; return; }
        Serial.println("VAD: dang thu giong noi");
        voiceRecording = true;
    }
    if (!recording) return;
    for (size_t i = 0; i < samples && count < 24000; ++i) recording[count++] = frame[i];
    if (vad.finished() || count == 24000) {
        if (!lastVoiceRequest || millis() - lastVoiceRequest >= 6000) {
            lastVoiceRequest = millis();
            Serial.printf("VAD: gui %u mau 8kHz\n", static_cast<unsigned>(count));
            if (!askGemini("", reinterpret_cast<uint8_t *>(recording), count * 2)) Serial.println(lastError);
        }
        free(recording); recording = nullptr; count = 0; voiceRecording = false; vad.reset(); cooldown = millis() + (lastError.isEmpty() ? 300 : 15000);
    }
}
void pollConnections() {
    static uint32_t pressedAt = 0;
    static uint32_t releasedAt = 0;
    static bool held = false, printed = false, armed = false;
    if (!armed) {
        if (digitalRead(0) == HIGH && millis() > 10000) {
            if (!releasedAt) releasedAt = millis();
            if (millis() - releasedAt > 500) armed = true;
        } else releasedAt = 0;
    }
    if (armed && digitalRead(0) == LOW) {
        if (!held) { pressedAt = millis(); held = true; }
        if (pressedAt && millis() - pressedAt >= 2000) { openSetup(); pressedAt = 0; }
    } else { pressedAt = 0; held = false; }
    if (setupUntil && !setupOpen()) {
        setupUntil = 0; WiFi.softAPdisconnect(true);
        if (bleStarted) {
            BLEDevice::getAdvertising()->stop();
            // Restart to reclaim BLE RAM before TLS/audio; credentials persist in NVS.
            ESP.restart();
        }
    }
    if (WiFi.status() == WL_CONNECTED && !printed) {
        Serial.println("Web: http://" + WiFi.localIP().toString()); printed = true;
    }
    if (WiFi.status() != WL_CONNECTED) printed = false;
    if (!wifiChanging && configQueue && xQueueReceive(configQueue, &candidate, 0) == pdTRUE) {
        wifiChanging = true; wifiStarted = millis(); WiFi.disconnect(); WiFi.begin(candidate.ssid, candidate.password);
    }
    if (wifiChanging) {
        if (WiFi.status() == WL_CONNECTED) {
            savedSsid = candidate.ssid; savedPassword = candidate.password;
            preferences.putString("ssid", savedSsid); preferences.putString("password", savedPassword);
            wifiChanging = false; lastError = "";
        } else if (millis() - wifiStarted > 20000) {
            WiFi.disconnect(); WiFi.begin(savedSsid.c_str(), savedPassword.c_str());
            wifiChanging = false; lastError = "WiFi moi that bai; dang khoi phuc mang cu";
        }
    }
    // ponytail: press-to-talk only; add VAD after measuring free heap with BLE and TLS.
}
