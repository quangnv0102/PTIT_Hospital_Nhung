#include "display.h"
#include <U8g2lib.h>

#define SCREEN_W  128
#define SCREEN_H   64

// ─── Chọn driver OLED ───────────────────────────────────────────────────
// U8g2 hỗ trợ cả SSD1306 (Wokwi) và SH1106 (phần cứng thật) trong cùng
// thư viện — chỉ khác tên constructor.
// Dùng SW_I2C (software I2C) với pin GPIO rõ ràng — hoạt động trên cả
// Wokwi lẫn phần cứng thật, không phụ thuộc Wire1.
// Tốc độ SW_I2C đủ nhanh cho OLED cập nhật 2Hz (500ms/lần).
#if USE_MOCK_DATA
    static U8G2_SSD1306_128X64_NONAME_F_SW_I2C oled(U8G2_R0, /*SCL=*/PIN_SCL1, /*SDA=*/PIN_SDA1, U8X8_PIN_NONE);
#else
    static U8G2_SH1106_128X64_NONAME_F_SW_I2C oled(U8G2_R0, /*SCL=*/PIN_SCL1, /*SDA=*/PIN_SDA1, U8X8_PIN_NONE);
#endif

// Font 5×8 pixel — vừa khít 128px chiều ngang (25 ký tự)
#define FONT       u8g2_font_5x8_tr
#define FONT_H     8   // chiều cao font
#define FONT_ASC   7   // ascent (baseline tính từ trên)

// Đếm số lần cập nhật để tạo hiệu ứng nhấp nháy
static uint32_t displayTick = 0;

void displaySetup() {
    oled.begin();
    oled.setFont(FONT);

    oled.clearBuffer();
    oled.drawStr(14, 30, "Patient Monitor");
    oled.drawStr(20, 42, "Khoi dong...");
    oled.sendBuffer();

    Serial.println("[Display] SSD1306 OK");
}

// ─── Helper: chuỗi viết tắt trạng thái ────────────────────────────────────
static const char* stateLabel(PatientState s) {
    switch (s) {
        case STATE_WARNING:  return "WARN";
        case STATE_CRITICAL: return "CRIT";
        case STATE_FALL:     return "FALL";
        default:             return "OK  ";
    }
}

// ─── Vẽ thông tin 1 bệnh nhân vào vùng y0..y0+15 ─────────────────────────
static void drawPatient(const PatientData* p, int y0, bool blink) {
    char buf[26];

    if (p->state != STATE_NORMAL && blink) {
        // Nhấp nháy: xoá vùng rồi vẽ cảnh báo
        oled.setDrawColor(0);
        oled.drawBox(0, y0, SCREEN_W, 16);
        oled.setDrawColor(1);
        snprintf(buf, sizeof(buf), "!! BN%d %s !!",
                 p->id + 1, stateLabel(p->state));
        oled.drawStr(0, y0 + FONT_ASC + 4, buf);
    } else {
        snprintf(buf, sizeof(buf), "BN%d HR:%-3d SpO2:%-3d%s",
                 p->id + 1, (int)p->hr, (int)p->spo2, stateLabel(p->state));
        oled.drawStr(0, y0 + FONT_ASC, buf);
    }
}

// ─── Hiển thị hướng dẫn WiFiManager AP mode ───────────────────────────────
void displayAPMode(const char* apSsid, const char* apPwd, const char* ip) {
    oled.clearBuffer();
    oled.setFont(FONT);

    oled.drawStr(0, FONT_ASC, "CAU HINH WIFI");
    oled.drawHLine(0, 10, SCREEN_W);

    oled.drawStr(0, 20 + FONT_ASC, "WiFi:");
    oled.drawStr(32, 20 + FONT_ASC, apSsid);

    oled.drawStr(0, 30 + FONT_ASC, "Pass:");
    oled.drawStr(32, 30 + FONT_ASC, apPwd);

    oled.drawStr(0, 44 + FONT_ASC, "Mo trinh duyet:");
    oled.drawStr(0, 54 + FONT_ASC, ip);

    oled.sendBuffer();
}

// ─── Render toàn bộ layout 2 bệnh nhân ────────────────────────────────────
void updateDisplay(PatientData patients[], int count) {
    displayTick++;
    bool blink = (displayTick % 2 == 0);

    oled.clearBuffer();
    oled.setFont(FONT);

    // BN#1 — vùng 0–15px
    if (count > 0) {
        drawPatient(&patients[0], 0, blink);
    }

    // Đường kẻ ngang — y=16
    oled.drawHLine(0, 16, SCREEN_W);

    // BN#2 — vùng 17–32px
    if (count > 1) {
        drawPatient(&patients[1], 17, blink);
    }

    // Đường kẻ ngang — y=33
    oled.drawHLine(0, 33, SCREEN_W);

    // Status bar — vùng 34–48px
    {
        char wifiStr[8];
        snprintf(wifiStr, sizeof(wifiStr), "WiFi:OK");

        unsigned long t = millis() / 1000UL;
        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu",
                 t / 3600UL, (t % 3600UL) / 60UL, t % 60UL);

        oled.drawStr(0, 36 + FONT_ASC, wifiStr);
        oled.drawStr(64, 36 + FONT_ASC, timeStr);
    }

    oled.sendBuffer();
}
