#pragma once
#include <Arduino.h>

// ===== Mock Data Flag =====
// 1 = dùng số giả lập (không cần phần cứng)
// 0 = đọc từ cảm biến thật
#define USE_MOCK_DATA    0

// ===== Số bệnh nhân =====
#define NUM_PATIENTS     2   // số đang dùng (triển khai demo)
#define MAX_PATIENTS     4   // kiến trúc hỗ trợ tối đa

// ===== TCA9548A Multiplexer =====
// 1 = có TCA9548A (dùng khi nối nhiều sensor cùng địa chỉ)
// 0 = không có TCA, sensor nối thẳng vào bus I2C
#define USE_TCA9548A     0

// ===== GPIO Pins =====
#define PIN_SDA0         21  // I2C Bus 0 (Wire)  — cảm biến BN#1
#define PIN_SCL0         22
#define PIN_SDA_P1       16  // I2C Bus 1 (Wire1) — cảm biến BN#2 (cùng hàng với GPIO21/22)
#define PIN_SCL_P1       17
#define PIN_SDA1         18  // OLED SW_I2C
#define PIN_SCL1         19
#define PIN_BUZZER        5
#define PIN_LED           2  // LED báo hiệu (optional)

// ===== I2C Addresses =====
#define ADDR_TCA9548A    0x70
#define ADDR_MAX30102    0x57
#define ADDR_MPU6050     0x68
#define ADDR_OLED        0x3C

// ===== Kênh TCA9548A =====
#define CH_MAX30102_P0   0   // MAX30102 bệnh nhân #1
#define CH_MAX30102_P1   1   // MAX30102 bệnh nhân #2
#define CH_MAX30102_P2   2   // dự phòng
#define CH_MAX30102_P3   3   // dự phòng
#define CH_MPU6050_P0    4   // MPU-6050 bệnh nhân #1
#define CH_MPU6050_P1    5   // MPU-6050 bệnh nhân #2
#define CH_MPU6050_P2    6   // dự phòng
#define CH_MPU6050_P3    7   // dự phòng

// ===== Ngưỡng cảnh báo SpO2 (%) =====
#define SPO2_WARNING     94.0f   // <94% → WARNING
#define SPO2_CRITICAL    90.0f   // <90% → CRITICAL

// ===== Ngưỡng cảnh báo nhịp tim (BPM) =====
#define HR_WARNING_LOW   55.0f   // <55 BPM → WARNING
#define HR_WARNING_HIGH  110.0f  // >110 BPM → WARNING
#define HR_CRITICAL_LOW  50.0f   // <50 BPM → CRITICAL
#define HR_CRITICAL_HIGH 120.0f  // >120 BPM → CRITICAL

// ===== Ngưỡng phát hiện ngã =====
#define FALL_IMPACT_G    2.5f    // SMA > 2.5g → phát hiện va đập
#define FALL_STILL_G     1.2f    // SMA < 1.2g → bất động sau ngã
#define FALL_CONFIRM_MS  1000    // cửa sổ xác nhận ngã (ms)

// ===== Timing =====
#define SENSOR_READ_MS       20      // 50Hz
#define DISPLAY_UPDATE_MS    500     // 2Hz
#define MQTT_PUBLISH_MS      1000    // 1Hz
#define TELEGRAM_COOLDOWN_MS 30000   // cooldown 30s chống spam

// ===== Lọc nhiễu =====
#define FILTER_WINDOW    5   // moving average window size

// ===== WiFi =====
// Wokwi simulator: SSID "Wokwi-GUEST" + password rỗng để có internet ảo.
// Khi nạp lên ESP32 thật: thay bằng SSID/password WiFi thật của bạn.
#define WIFI_SSID        "Wokwi-GUEST"
#define WIFI_PASSWORD    ""

// ===== Telegram Bot =====
#define BOT_TOKEN        "8741542471:AAEIsYNh8LDLgsou8CbJowO-HFVCOps1-R4"

// Bac si — nhan canh bao cua TAT CA benh nhan
#define CHAT_ID_DOCTOR       "8772138130"

// Nguoi nha — moi BN 1 chat ID, CHI nhan canh bao cua BN do
// Neu de trong ("") se bo qua (khong gui cho nguoi nha BN do)
#define CHAT_ID_FAMILY_BN1   "8487264082"   // <- Dan chat_id cua nguoi nha BN#1 vao day
#define CHAT_ID_FAMILY_BN2   "1421126628"   // <- Dan chat_id cua nguoi nha BN#2 vao day

// Backward compat: CHAT_ID cu van tro den bac si (cho code goi sendTelegram() truc tiep)
#define CHAT_ID              CHAT_ID_DOCTOR

// ===== Ten benh nhan (hien thi trong Telegram) =====
// De trong "" -> hien thi mac dinh "Benh nhan #N"
#define PATIENT_NAME_BN1     "Nguyen Van Minh"
#define PATIENT_NAME_BN2     "Pham Thi Lan"
#define PATIENT_NAME_BN3     ""
#define PATIENT_NAME_BN4     ""

// ===== MQTT =====
#define MQTT_BROKER      "broker.hivemq.com"
#define MQTT_PORT        1883
#define MQTT_CLIENT_ID   "esp32_patient_monitor"

// ===================================================================
// Cấu trúc dữ liệu chính
// ===================================================================

// Trạng thái FSM của bệnh nhân
enum PatientState {
    STATE_NORMAL   = 0,  // xanh — bình thường
    STATE_WARNING  = 1,  // vàng — gần ngưỡng nguy hiểm
    STATE_CRITICAL = 2,  // đỏ  — vượt ngưỡng nguy hiểm
    STATE_FALL     = 3   // tím — phát hiện ngã
};

// Dữ liệu đầy đủ của 1 bệnh nhân
struct PatientData {
    int         id;              // số thứ tự (0 hoặc 1)
    float       hr;              // nhịp tim sau lọc (BPM)
    float       spo2;            // SpO2 sau lọc (%)
    float       ax, ay, az;      // gia tốc 3 trục raw (g)
    float       sma;             // Signal Magnitude Area
    bool        fallDetected;    // cờ phát hiện ngã
    PatientState state;          // trạng thái hiện tại
    PatientState prevState;      // trạng thái trước đó
    unsigned long lastUpdate;    // millis() lần đọc cuối
    unsigned long lastAlertSent; // millis() lần gửi Telegram cuối
    bool        sensorConnected; // cảm biến có kết nối không
};

// Buffer lọc nhiễu riêng cho từng bệnh nhân
struct FilterBuffer {
    float hrBuffer[FILTER_WINDOW];   // 5 mẫu HR gần nhất
    float spo2Buffer[FILTER_WINDOW]; // 5 mẫu SpO2 gần nhất
    int   index;                     // vị trí ghi tiếp theo
    bool  filled;                    // buffer đã đầy lần đầu chưa
};
