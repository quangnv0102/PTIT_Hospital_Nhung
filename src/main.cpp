#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "config.h"

// WiFiManager chỉ dùng ở chế độ phần cứng thật (không cần khi chạy Wokwi)
#if !USE_MOCK_DATA
#include <WiFiManager.h>
#endif
#include "sensor/sensor.h"
#include "utils/filter.h"
#include "fall_detect/fall_detect.h"
#include "alert/alert.h"
#include "display/display.h"
#include "network/telegram.h"
#include "network/mqtt_client.h"

// ─── Dữ liệu toàn cục ─────────────────────────────────────────────────────
PatientData  patients[NUM_PATIENTS];
FilterBuffer filterBuf[NUM_PATIENTS];

// ─── Helper: chuyển state enum sang text tieng Viet khong dau ────────────
// Padding cung do rong 13 ky tu (theo "PHAT HIEN NGA") de cot log thang hang
static const char* stateText(PatientState s) {
    switch (s) {
        case STATE_NORMAL:   return "BINH THUONG  ";
        case STATE_WARNING:  return "CANH BAO     ";
        case STATE_CRITICAL: return "NGUY HIEM    ";
        case STATE_FALL:     return "PHAT HIEN NGA";
        default:             return "KHONG RO     ";
    }
}

// Mutex bảo vệ truy cập I2C Bus 0 giữa 2 task bệnh nhân
SemaphoreHandle_t wireMutex;

// ─── Khởi tạo WiFi ────────────────────────────────────────────────────────
// Mock mode (Wokwi): dùng WiFi hardcoded trong config.h (Wokwi-GUEST)
// Real hardware:     dùng WiFiManager captive portal — lần đầu không có WiFi
//                    đã lưu, ESP32 tự phát AP "Patient-Monitor-Setup", user
//                    cắm điện thoại vào và chọn WiFi nhà/phòng bệnh. Lần sau
//                    tự kết nối bằng credentials đã lưu trong flash.
static void wifiConnect() {
#if USE_MOCK_DATA
    Serial.printf("[WiFi] Ket noi %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int timeout = 20; // 10 giây
    while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] OK — IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] FAIL — tiep tuc khong co mang");
    }
#else
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);   // tối đa 3 phút ở AP mode
    wm.setConnectTimeout(20);         // tối đa 20s thử kết nối WiFi đã lưu

    // Callback được gọi khi WiFiManager chuyển sang AP mode
    wm.setAPCallback([](WiFiManager* myWM) {
        Serial.println("\n[WiFi] Khong co WiFi da luu — mo captive portal");
        Serial.println("[WiFi] SSID: Patient-Monitor-Setup");
        Serial.println("[WiFi] Pass: 12345678");
        Serial.println("[WiFi] Vao: http://192.168.4.1");
        displayAPMode("Patient-Monitor", "12345678", "192.168.4.1");
    });

    // autoConnect():
    //   1. Thử kết nối bằng credentials đã lưu trong NVS (flash ESP32)
    //   2. Nếu thất bại → phát AP với SSID + password đã cho
    //   3. User cắm điện thoại vào AP → captive portal tự mở
    //   4. User chọn WiFi + nhập mật khẩu → lưu vào flash → ESP32 restart
    Serial.println("[WiFi] Kiem tra WiFi da luu...");
    if (wm.autoConnect("Patient-Monitor-Setup", "12345678")) {
        Serial.printf("[WiFi] OK — IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Timeout portal — khoi dong lai sau 5s");
        delay(5000);
        ESP.restart();
    }
#endif
}

// Xoá WiFi đã lưu — dùng khi cần cấu hình lại. Không gọi ở code thường,
// chỉ để sẵn cho trường hợp cần reset từ main.cpp setup() khi debug.
void wifiResetSettings() {
#if !USE_MOCK_DATA
    WiFiManager wm;
    wm.resetSettings();
    Serial.println("[WiFi] Da xoa credentials, restart de cau hinh lai");
    delay(1000);
    ESP.restart();
#endif
}

// ─── Task bệnh nhân (chạy trên Core 0, priority 3) ────────────────────────
// Mỗi bệnh nhân có 1 task riêng, nhận patientId qua tham số
static void taskPatient(void* pvParameters) {
    int id = (int)(intptr_t)pvParameters;
    Serial.printf("[Task] taskPatient%d started on core %d\n", id, xPortGetCoreID());

    for (;;) {
        // ── Đọc cảm biến (bảo vệ I2C Bus 0 bằng mutex) ──────────────────
        if (xSemaphoreTake(wireMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            readPatient(id, &patients[id]);
            xSemaphoreGive(wireMutex);
        }

        // ── Lọc nhiễu moving average (window 5) ─────────────────────────
        patients[id].hr   = movingAverage(&filterBuf[id],
                                          filterBuf[id].hrBuffer,
                                          patients[id].hr);
        patients[id].spo2 = movingAverage(&filterBuf[id],
                                          filterBuf[id].spo2Buffer,
                                          patients[id].spo2);
        advanceFilter(&filterBuf[id]);

        // ── Tính SMA ────────────────────────────────────────────────────
        patients[id].sma = calcSMA(patients[id].ax,
                                   patients[id].ay,
                                   patients[id].az);

        // ── Phát hiện ngã ────────────────────────────────────────────────
        if (detectFall(id, patients[id].ax, patients[id].ay, patients[id].az)) {
            patients[id].fallDetected = true;
        }

        // ── Kiểm tra ngưỡng (alert FSM) ─────────────────────────────────
        bool stateChanged = checkThresholds(&patients[id]);

        // ── Cảnh báo nếu state thay đổi ─────────────────────────────────
        if (stateChanged) {
            triggerBuzzer(patients[id].state);
            sendAlert(&patients[id]);

            Serial.printf("\n>>> [BN#%d] Doi trang thai: %s -> %s  |  "
                          "Nhip tim=%.0f BPM, SpO2=%.0f%%, SMA=%.2fg\n\n",
                          id + 1,
                          stateText(patients[id].prevState),
                          stateText(patients[id].state),
                          patients[id].hr,
                          patients[id].spo2,
                          patients[id].sma);
        }

        // ── Xử lý sau phát hiện ngã ─────────────────────────────────────
        if (patients[id].fallDetected) {
            patients[id].fallDetected = false;
            resetFallState(id);
        }

        // ── Cảm biến mất kết nối ─────────────────────────────────────────
        if (!patients[id].sensorConnected) {
            unsigned long now = millis();
            if ((now - patients[id].lastAlertSent) >= TELEGRAM_COOLDOWN_MS) {
                // Lay ten benh nhan tu config
                static const char* names[MAX_PATIENTS] = {
                    PATIENT_NAME_BN1, PATIENT_NAME_BN2,
                    PATIENT_NAME_BN3, PATIENT_NAME_BN4
                };
                String patName = (names[id] && names[id][0])
                    ? String(names[id]) + " (BN#" + String(id+1) + ")"
                    : "Benh nhan #" + String(id+1);
                String msg = "MAT KET NOI CAM BIEN -- " + patName +
                             "\nKiem tra lai ket noi vat ly";
                sendTelegram(msg);
                patients[id].lastAlertSent = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_READ_MS));
    }
}

// ─── Task hiển thị OLED (Core 1, priority 1) ──────────────────────────────
static void taskDisplay(void* pvParameters) {
    Serial.printf("[Task] taskDisplay started on core %d\n", xPortGetCoreID());
    static int debugCount = 0;
    for (;;) {
        updateDisplay(patients, NUM_PATIENTS);

        // Debug: in bang trang thai moi 2 giay (moi 4 lan update × 500ms)
        if (++debugCount >= 4) {
            debugCount = 0;
            Serial.println("--------------------------------------------------------------------------------");
            for (int i = 0; i < NUM_PATIENTS; i++) {
                const PatientData& p = patients[i];
                Serial.printf("BN#%d | Nhip tim:%3.0f BPM | SpO2:%3.0f%% | "
                              "Gia toc(%+.2f,%+.2f,%+.2f) SMA=%.2fg | Trang thai: %s | Cam bien: %s\n",
                              i + 1,
                              p.hr, p.spo2,
                              p.ax, p.ay, p.az, p.sma,
                              stateText(p.state),
                              p.sensorConnected ? "OK " : "MAT ket noi");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_MS));
    }
}

// ─── Task WiFi + MQTT (Core 1, priority 2) ───────────────────────────────
// WiFi kết nối ở nền — KHÔNG chặn monitoring. Nếu không có WiFi,
// hệ thống vẫn đo + hiển thị OLED + kêu buzzer bình thường.
static void taskNetwork(void* pvParameters) {
    Serial.printf("[Task] taskNetwork started on core %d\n", xPortGetCoreID());

    // Bước 1: kết nối WiFi (blocking nhưng ở task riêng, không ảnh hưởng sensor)
    wifiConnect();

    // Bước 2: khởi tạo MQTT sau khi WiFi xong
    if (WiFi.status() == WL_CONNECTED) {
        mqttSetup();
    }

    // ── TEST Telegram 1 lần khi boot (xoá sau khi test xong) ─────────────
    if (WiFi.status() == WL_CONNECTED) {
        sendTelegram("Test tu ESP32 Patient Monitor — he thong da khoi dong");
    }

    // Bước 3: vòng lặp publish MQTT
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            mqttLoop();
            for (int i = 0; i < NUM_PATIENTS; i++) {
                publishPatient(&patients[i]);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISH_MS));
    }
}

// ─── setup() ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n===== Patient Monitor System =====");

    // GPIO
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // Mutex bảo vệ I2C Bus 0
    wireMutex = xSemaphoreCreateMutex();

    // Khởi tạo các module — sensor + display TRƯỚC, WiFi SAU
    // Hệ thống bắt đầu đo ngay, WiFi là tùy chọn
    sensorSetup();
    displaySetup();
    fallDetectInit();

    // Khởi tạo mảng dữ liệu bệnh nhân và filter buffer
    for (int i = 0; i < NUM_PATIENTS; i++) {
        memset(&patients[i],   0, sizeof(PatientData));
        memset(&filterBuf[i],  0, sizeof(FilterBuffer));
        patients[i].id           = i;
        patients[i].state        = STATE_NORMAL;
        patients[i].prevState    = STATE_NORMAL;
        patients[i].sensorConnected = true;
        // Điền sẵn giá trị khởi đầu hợp lý để filter ổn định nhanh hơn
        for (int w = 0; w < FILTER_WINDOW; w++) {
            filterBuf[i].hrBuffer[w]   = (i == 0) ? 72.0f : 68.0f;
            filterBuf[i].spo2Buffer[w] = (i == 0) ? 97.0f : 98.0f;
        }
        filterBuf[i].index  = 0;
        filterBuf[i].filled = true;
    }

    // Tạo FreeRTOS tasks — 1 task per bệnh nhân trên Core 0
    char taskName[16];
    for (int i = 0; i < NUM_PATIENTS; i++) {
        snprintf(taskName, sizeof(taskName), "taskPatient%d", i);
        xTaskCreatePinnedToCore(taskPatient, taskName,
                                8192, (void*)(intptr_t)i,
                                3, NULL, 0);
    }

    // taskDisplay — Core 1, priority 1
    xTaskCreatePinnedToCore(taskDisplay, "taskDisplay",
                            4096, NULL,
                            1, NULL, 1);

    // taskNetwork — Core 1, priority 2 (WiFi + MQTT chạy nền)
    xTaskCreatePinnedToCore(taskNetwork, "taskNetwork",
                            8192, NULL,
                            2, NULL, 1);

    Serial.println("[Setup] Tat ca tasks da khoi chay");
    digitalWrite(PIN_LED, HIGH); // LED bật = hệ thống sẵn sàng
}

// ─── loop() ───────────────────────────────────────────────────────────────
// Không chứa logic — toàn bộ xử lý nằm trong FreeRTOS tasks
void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
