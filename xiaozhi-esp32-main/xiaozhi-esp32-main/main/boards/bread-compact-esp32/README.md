# Phần cứng đã chỉnh cho ESP32 của dự án OLED_TEST

Board này đã được chỉnh theo đúng dây hiện tại:

- INMP441: WS=P4, SCK=P5, SD=P18
- MAX98357A: BCLK=P15, LRC=P16, DIN=P17
- OLED SSD1306: SDA=P21, SCL=P22
- Nút BOOT mặc định: GPIO0

Nối `SD/EN` của MAX98357A lên 3V3 và nối loa giữa `SPK+`/`SPK-`.

## 编译配置命令

**配置编译目标为 ESP32：**

```bash
idf.py set-target esp32
```

**打开 menuconfig：**

```bash
idf.py menuconfig
```

**选择板子：**

```
Xiaozhi Assistant -> Board Type -> 面包板 ESP32 DevKit
```

**编译：**

```bash
idf.py build
```