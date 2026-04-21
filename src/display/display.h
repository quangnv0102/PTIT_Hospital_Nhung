#pragma once
#include "../config.h"

// Khởi tạo OLED SSD1306 trên I2C Bus 1 (GPIO18=SDA, GPIO19=SCL).
// Gọi trong setup() của main.
void displaySetup();

// Render toàn bộ layout lên màn hình OLED 128×64px.
// Hiển thị HR, SpO2, trạng thái cho count bệnh nhân.
// Nhấp nháy vùng tương ứng khi có cảnh báo.
void updateDisplay(PatientData patients[], int count);

// Hiển thị hướng dẫn khi ESP32 đang ở AP mode (captive portal WiFiManager).
// Người dùng cần cắm điện thoại vào AP này để cấu hình WiFi nhà.
void displayAPMode(const char* apSsid, const char* apPwd, const char* ip);
