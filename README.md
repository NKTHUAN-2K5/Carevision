# OLED_TEST - ESP32 + OLED I2C

## Giao diện và chế độ độc lập (bản cập nhật)

### Nghe tự động

Firmware hiện dùng `gemini-3.5-flash-lite`, mặc định bật nghe tự động. Web có nút bật/tắt. VAD năng lượng hiệu chỉnh nền ban đầu, bắt âm lượng tăng, kết thúc sau khoảng 0,72 giây yên lặng; giới hạn mỗi bản thu 3 giây, PCM16 8 kHz. Loa đang phát hoặc chế độ cấu hình đang mở thì không thu tự động. Mỗi lần gọi tự động cách nhau ít nhất 6 giây; vẫn có thể hết quota ngày, chưa có giới hạn ngày trong firmware. Đây không phải wake word: tiếng động lớn có thể kích hoạt nhầm. Chưa có bộ nhớ hội thoại nhiều lượt. Hạn chế RAM: bản thu ngắn và giảm mẫu để dành RAM cho TLS; nâng lên PSRAM/streaming khi cần câu dài. Server ASR dự phòng cũ cần chỉnh hỗ trợ 8 kHz trước khi dùng; ưu tiên Gemini trực tiếp.

- Web mới: Trò chuyện, Kết nối, Chẩn đoán; mở IP in trong Serial (`Web: http://...`).
- Khi có key trong NVS, mic trên mạch và chat web gọi Gemini trực tiếp qua HTTPS có xác minh chứng chỉ. Không cần server máy tính. Model hiện dùng `gemini-3.6-flash`; lỗi hạn mức/model/mạng được hiện trên web.
- Nhập key qua USB Serial bằng lệnh `key YOUR_KEY` (115200 baud), hoặc giữ BOOT 2 giây, kết nối WiFi riêng `Carevision-Setup` bằng mật khẩu ngẫu nhiên in trên Serial, mở `http://192.168.4.1`, mục Chẩn đoán. Không gửi key qua WiFi công cộng. Key không được trả về API trạng thái; NVS chưa mã hóa nên không chống trích xuất vật lý.
- BOOT giữ 2 giây mở cửa sổ cấu hình 120 giây: quét/đổi WiFi, đổi endpoint, ghép đôi BLE. WiFi chỉ lưu khi kết nối thành công; thất bại sau 20 giây thì thử khôi phục cấu hình cũ.
- BLE `Carevision` có mã ghép đôi in Serial, chỉ nhận cấu hình trong cửa sổ BOOT. Hết cửa sổ mạch khởi động lại để thu hồi RAM Bluetooth trước khi gọi AI. Không truyền âm thanh Bluetooth.
- BLE trên web và mic trình duyệt cần Chrome/Edge tương thích, HTTPS hoặc localhost. Safari/iPhone dùng cấu hình WiFi thay thế. Proxy tùy chọn: `python3 tools/chat_proxy.py --esp32 http://IP_ESP32`, mở `http://127.0.0.1:8080`.
- Bấm **Mic trên mạch** để thu 2 giây, hoặc gõ `mic` qua Serial. Chưa nghe liên tục, chưa có wake word, chưa nhớ hội thoại nhiều lượt. Tin nhắn web giữ trong tab, mất khi tải lại.
- Giọng nói vẫn dùng dịch vụ Google Translate TTS hiện có, phụ thuộc Internet; chưa bảo đảm dịch vụ này luôn khả dụng. Giới hạn hiện tại: trong lúc chờ HTTP AI, web đồng bộ chưa xử lý lệnh dừng; dừng loa sau khi yêu cầu hiện tại hoàn tất.
- Không dùng endpoint web không xác thực trên mạng không tin cậy: token chống gửi chéo trang không phải đăng nhập người dùng. Cấu hình nhạy cảm còn yêu cầu cửa sổ BOOT; key web chỉ nhận qua AP riêng.
- Partition `huge_app.csv` dành 3 MB cho firmware BLE + TLS; **không hỗ trợ OTA** trong bố cục này. NVS giữ nguyên vị trí; không cần xóa flash.

Kiểm tra: `python3 tools/check_firmware.py`, `pio run`. Build không thay thế kiểm thử BLE, âm thanh, phục hồi WiFi và hội thoại trên mạch thật.

Các mục server ASR bên dưới là chế độ cũ/dự phòng khi chưa có Gemini key trên mạch.

## Phần cứng mặc định
- Board: ESP32 DevKit V1
- OLED: SSD1306 128x64, I2C, địa chỉ 0x3C
- OLED GND -> ESP32 GND
- OLED VCC -> ESP32 3V3
- OLED SDA -> ESP32 GPIO 21
- OLED SCL -> ESP32 GPIO 22
- MAX98357A VIN -> ESP32 5V; GND -> GND; LRC -> P16; BCLK -> P15; DIN -> P17
- INMP441 VDD -> ESP32 3V3; GND -> GND; L/R -> GND; WS -> P4; SCK -> P5; SD -> P18

## Chạy bằng VS Code + PlatformIO
1. Cài extension **PlatformIO IDE** trong VS Code.
2. Giải nén thư mục `OLED_TEST`.
3. Trong VS Code chọn File > Open Folder và mở thư mục `OLED_TEST`.
4. Cắm ESP32 bằng cáp USB dữ liệu.
5. Nhấn **Build** (PlatformIO: Build) để biên dịch.
6. Nhấn **Upload** (PlatformIO: Upload) để nạp code.
7. Mở **Monitor** (PlatformIO: Monitor) để xem Serial ở 115200 baud.

PlatformIO sẽ tự tải thư viện Adafruit SSD1306 và Adafruit GFX khi build.

## Wi-Fi, khuôn mặt và chat

Mã mẫu kết nối Wi-Fi, hiển thị khuôn mặt vui/buon/suy nghĩ/ngủ trên OLED,
và nhận tin nhắn qua Serial Monitor. Để bật Wi-Fi, tạo file `include/secrets.h` (file này đã được `.gitignore`
bỏ qua) dựa trên `include/secrets.example.h`:

```cpp
#pragma once

#define WIFI_SSID "Ten_WiFi_cua_ban"
#define WIFI_PASSWORD "Mat_khau_WiFi_cua_ban"
```

Sau khi nạp, mở Serial Monitor ở 115200 baud. Khi kết nối thành công, Serial
in địa chỉ IP; mở địa chỉ đó trên trình duyệt để chat qua giao diện web.
Phản hồi hiện là phản hồi cục bộ an toàn, không cần API hay gửi dữ liệu ra
ngoài. Muốn trò chuyện bằng AI thật, cần bổ sung máy chủ/API và không nên
ghi khóa API trực tiếp vào firmware.

### Dùng nút Mic bằng proxy localhost

Chrome mới không cho microphone trên địa chỉ HTTP của ESP32. Có thể chạy proxy
trên máy tính để trang được mở từ `localhost`:

```powershell
python tools\chat_proxy.py --esp32 http://192.168.1.42
```

Sau đó mở `http://127.0.0.1:8080`, cấp quyền microphone và bấm **Mic**. Máy
tính phải cùng mạng Wi-Fi với ESP32. Dừng proxy bằng `Ctrl+C`.

## Loa MAX98357A và micro INMP441

Firmware dùng Wi-Fi để lấy giọng tiếng Việt từ dịch vụ Google Translate TTS,
giải mã MP3 trên ESP32 và phát qua MAX98357A. Khi khởi động, robot sẽ nói:
`Xin chao! Toi la robot Carevidssio.` Mỗi phản hồi chat cũng được phát qua loa.
Phần I2S của INMP441 dùng mô hình simplex 32-bit, slot trái và bus riêng theo
cách Xiaozhi tổ chức audio; firmware đã đọc được mẫu mic. Firmware hiện chưa
chuyển âm thanh mic thành văn bản native. Nút `Mic` trên trang web dùng
microphone của điện thoại/máy tính qua trình duyệt, không dùng INMP441.

### Nhận dạng giọng nói từ INMP441 bằng Whisper local

Whisper chạy cục bộ trên máy tính, không cần API key. Máy tính cần Docker và
phải cùng mạng với ESP32. Từ thư mục dự án, chạy:

```powershell
docker build -f tools\Dockerfile.whisper -t oled-whisper-asr tools
docker run --rm -p 8787:8787 -e WHISPER_MODEL=small oled-whisper-asr
```

Lần chạy đầu sẽ tải model Whisper (model `small` cần nhiều dung lượng và CPU).
Nếu máy yếu, dùng `WHISPER_MODEL=base`. Kiểm tra server bằng
`http://127.0.0.1:8787/health`. Tạo `include/secrets.h` và thêm:

```cpp
#define ASR_SERVER_URL "http://DIA_CHI_MAY_TINH:8787/transcribe"
```

Sau khi nạp lại firmware, gửi lệnh `mic` trong Serial Monitor để ghi 3 giây
âm thanh từ INMP441. Hoặc mở `http://DIA_CHI_ESP32/listen`. Văn bản nhận dạng
sẽ được xử lý như tin nhắn và phát qua MAX98357A.

Khi khởi động, firmware phát bíp khoảng 1,5 giây trước khi kết nối Wi-Fi.
Nếu không nghe thấy bíp, trước tiên nối chân `SD/EN` của MAX98357A lên `3V3`
(không để hở), nối loa giữa `SPK+` và `SPK-` (không nối một đầu loa xuống
GND), rồi kiểm tra VIN=5V và GND chung. Nếu đã nghe bíp nhưng không nghe TTS,
kiểm tra ESP32 đã kết nối Wi-Fi vì TTS lấy âm thanh qua mạng.

## Nếu không hiển thị
- Kiểm tra dây và nguồn 3.3V.
- Thử đổi `#define OLED_ADDRESS 0x3C` thành `0x3D`.
- Xác nhận màn hình là SSD1306 I2C. Một số màn hình dùng SH1106 cần thư viện/driver khác.
- Nếu dùng ESP32 board khác, kiểm tra lại chân SDA/SCL.
