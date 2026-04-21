# PHÂN CÔNG NHÓM — BÀI TẬP LỚN XÂY DỰNG HỆ THỐNG NHÚNG

**Đề tài:** Hệ thống theo dõi nhịp tim, SpO₂ và phát hiện ngã cho bệnh nhân

**Lớp:** D22CNPM02 — **Nhóm:** 4

**Thành viên:**
- Đàm Anh Đức — B22DCCN219
- Lê Phương Nam — B22DCCN555
- Vũ Hoàng Nam — B22DCCN567
- Nguyễn Văn Nhất — B22DCCN579
- Nguyễn Việt Quang — B22DCCN651

---

## 📊 Tổng quan phân công

| TV | Module chính | LOC | Thời gian | Độ khó |
|----|--------------|-----|-----------|--------|
| **Dam Anh Duc** | MAX30102 driver + Beat Detector + HR/SpO2 | 240 | 24h | ⭐⭐⭐⭐ |
| **Nguyen Van Nhat** | MPU6050 driver + Fall detection FSM | 240 | 24h | ⭐⭐⭐⭐ |
| **Nguyen Viet Quang** | FreeRTOS tasks + Mutex I2C + Scheduling | 240 | 24h | ⭐⭐⭐⭐ |
| **Le Phuong Nam** | Alert FSM + Buzzer + OLED display | 240 | 24h | ⭐⭐⭐⭐ |
| **Vu Hoang Nam** | WiFi + MQTT + Telegram phân quyền | 240 | 24h | ⭐⭐⭐⭐ |

**Tổng:** 1200 LOC ~ 120h ~ **24h/người**

Mỗi người có:
- 1 driver hardware riêng
- 1 thuật toán / FSM độc lập
- Phần báo cáo 3 trang + thuyết trình 2 phút

---

## 👥 Chi tiết từng thành viên

### 👤 TV1 — Cảm biến PPG (MAX30102) + HR + SpO2

**Người phụ trách:** Dam Anh Duc

**File phụ trách:**
- `src/sensor/sensor.cpp` — phần MAX30102 (dòng 82-340)
- `src/utils/filter.cpp` — moving average

**Công việc chi tiết (~240 LOC):**

#### 1. Driver I2C MAX30102 (60 LOC)
- Init FIFO, cấu hình LED power 0x20-0x7F, pulse width 411μs, ADC range 16384
- `maxSensor.begin()` + `setup()` trên Wire bus tương ứng mỗi BN
- Quick check ngón tay: IR < 7000 → skip

#### 2. Beat Detector per-patient (100 LOC)
- Struct `BeatDetector` đóng gói 12 biến state (IR_AC, cbuf FIR, offset...)
- `bd_averageDC()` — EMA DC tracker
- `bd_lowPassFIR()` — FIR 23-tap với hệ số chuẩn SparkFun
- `bd_checkForBeat()` — zero-crossing detection + min/max biên độ

#### 3. Tính HR (40 LOC)
- Khoảng cách 2 nhịp (300-2000ms hợp lệ)
- Trung bình 4 nhịp gần nhất (RATE_SIZE)
- Công thức `bpm = 60000 / delta`

#### 4. Tính SpO2 + Moving Average (40 LOC)
- Tracker min/max IR & Red
- Công thức R-ratio: `R = (AC_Red/DC_Red) / (AC_IR/DC_IR)`
- `SpO2 = 110 − 25R`, constrain 70-100%
- EMA smoothing α=0.3
- Moving average filter window=5 (trong `filter.cpp`)

**Báo cáo (3 trang):**
- Trang 1: Nguyên lý PPG, 2 LED 660nm+940nm, sóng AC/DC
- Trang 2: Thuật toán beat detection (FIR, zero-crossing)
- Trang 3: Công thức R-ratio + SpO2 xấp xỉ tuyến tính

**Demo (1 phút):** Đặt ngón tay vào MAX → HR + SpO2 hiện trên OLED sau 10-15 giây

---

### 👤 TV2 — Cảm biến IMU (MPU-6050/6500) + Phát hiện ngã

**Người phụ trách:** Nguyen Van Nhat

**File phụ trách:**
- `src/sensor/sensor.cpp` — phần MPU (dòng 28-80, 175-232, 342-358)
- `src/fall_detect/fall_detect.cpp` + `.h`
- `src/utils/filter.cpp` — hàm `calcSMA()`

**Công việc chi tiết (~240 LOC):**

#### 1. Driver raw I2C MPU (100 LOC)
- Bypass Adafruit → hỗ trợ MPU-6050 (0x68), MPU-6500 (0x70), MPU-9250 (0x71-73), clone (0x98)
- `mpuReadReg()`, `mpuWriteReg()` helper raw I2C
- `mpuTryInit()`: đọc WHO_AM_I → wake chip (PWR_MGMT_1=0) → set range ±2g
- Auto-detect địa chỉ 0x68 rồi fallback 0x69
- `mpuReadAccel()`: đọc 6 byte từ register 0x3B → quy đổi /16384 ra đơn vị g

#### 2. FSM Phát hiện ngã (80 LOC)
- Struct `FallDetector` per-patient (state, impactTime)
- 3 state nội bộ: IDLE → IMPACT → CONFIRM
- Transition: SMA > 2.5g → IMPACT + ghi timestamp
- Trong 1000ms nếu SMA < 1.2g → CONFIRM → trả true
- Timeout 1s không bất động → false alarm, reset IDLE
- `resetFallState()` sau khi xử lý cảnh báo

#### 3. Tính SMA + kiểm thử (60 LOC)
- `calcSMA(ax, ay, az) = sqrt(ax² + ay² + az²)`
- Test 4 tư thế: nằm phẳng (az≈1), lật úp (az≈-1), đứng cạnh (ax/ay≈±1)
- Loại false alarm cho ho mạnh, vỗ bàn (không bất động sau va đập)

**Báo cáo (3 trang):**
- Trang 1: Cảm biến IMU 6-DOF, nguyên lý gia tốc kế MEMS
- Trang 2: Công thức SMA, lý do chọn ngưỡng 2.5g/1.2g
- Trang 3: Sơ đồ FSM 3 trạng thái + cách loại false alarm

**Demo (1 phút):** Ném MPU xuống gối → trigger FALL → buzzer + Telegram

---

### 👤 TV3 — FreeRTOS Scheduling & Đồng bộ hóa

**Người phụ trách:** Nguyen Viet Quang

**File phụ trách:** toàn bộ `src/main.cpp`

**Công việc chi tiết (~240 LOC):**

#### 1. Khởi tạo hệ thống (50 LOC)
- GPIO: Buzzer OUTPUT, LED OUTPUT
- Khởi tạo `wireMutex = xSemaphoreCreateMutex()`
- Gọi `sensorSetup()`, `displaySetup()`, `fallDetectInit()`
- Khởi tạo mảng `patients[]` + `filterBuf[]` với giá trị seed hợp lý
- Điền sẵn 72/97 và 68/98 cho filter buffer ổn định nhanh

#### 2. Tạo 4 FreeRTOS tasks (40 LOC)
- `taskPatient0` + `taskPatient1`: Core 0, priority 3, stack 8192 bytes, chu kỳ 20ms (50Hz)
- `taskDisplay`: Core 1, priority 1, stack 4096 bytes, chu kỳ 500ms
- `taskNetwork`: Core 1, priority 2, stack 8192 bytes, chu kỳ 1000ms
- Dùng `xTaskCreatePinnedToCore()` pin vào core cụ thể

#### 3. taskPatient loop (80 LOC)
- Lấy mutex timeout 50ms → gọi `readPatient()` → giải phóng
- Gọi moving average, calcSMA
- Gọi `detectFall()`, `checkThresholds()`
- Trigger buzzer + sendAlert nếu state đổi
- Handle `sensorConnected=false` (gửi Telegram "MAT KET NOI")
- `vTaskDelay(pdMS_TO_TICKS(20))`

#### 4. taskDisplay + taskNetwork + helpers (70 LOC)
- Log định kỳ mỗi 2s (format tiếng Việt không dấu)
- Helper `stateText()` convert enum → text padding 13 ký tự
- Log ALERT khi state đổi

**Báo cáo (3 trang):**
- Trang 1: FreeRTOS concepts (task, priority, semaphore), dual-core ESP32
- Trang 2: Bảng 4 task + lý do pin core + phân tích tải CPU
- Trang 3: Mutex I2C → tránh race condition khi 2 BN đọc Bus 0

**Demo (1 phút):** Serial Monitor → 4 task chạy đồng thời, không deadlock

---

### 👤 TV4 — FSM Cảnh báo, Buzzer, Hiển thị OLED

**Người phụ trách:** Le Phuong Nam

**File phụ trách:**
- `src/alert/alert.cpp` + `.h`
- toàn bộ `src/display/display.cpp` + `.h`

**Công việc chi tiết (~240 LOC):**

#### 1. FSM Alert bệnh nhân (60 LOC) — trong `alert.cpp`
- Hàm `checkThresholds()`: so sánh HR/SpO2 với 4 ngưỡng
  - SPO2_WARNING (94%), SPO2_CRITICAL (90%)
  - HR_WARNING_LOW (55), HR_WARNING_HIGH (110)
  - HR_CRITICAL_LOW (50), HR_CRITICAL_HIGH (120)
- Ưu tiên FALL > CRITICAL > WARNING > NORMAL
- Update `state`, `prevState`, trả về `stateChanged`
- Handle `sensorConnected=false` separate

#### 2. Build Alert Message (40 LOC)
- Hàm `buildAlertMessage()` sinh chuỗi Telegram theo state
- 4 template khác nhau cho WARNING/CRITICAL/FALL/NORMAL
- Timestamp HH:MM:SS từ `millis()`

#### 3. Điều khiển Buzzer (40 LOC)
- `triggerBuzzer(state)` dùng `tone()` + `noTone()` (Timer HW ESP32 tạo PWM)
- WARNING: 2 kHz × 10 tiếng (500ms + 700ms nghỉ)
- CRITICAL: 3 kHz × 15 tiếng (400ms + 500ms nghỉ)
- FALL: 4 kHz × 20 tiếng (300ms + 350ms nghỉ)
- `vTaskDelay` giữa các tiếng

#### 4. OLED driver U8g2 (100 LOC)
- `displaySetup()`: init SH1106/SSD1306 SW_I2C GPIO18/19
- `updateDisplay()`: layout 3 vùng (BN#1 0-15px, BN#2 17-32px, Status 34-48px)
- Helper `stateLabel()`: OK/WARN/CRIT/FALL
- Hiệu ứng nhấp nháy 2Hz khi cảnh báo (`drawBox` invert)
- `displayAPMode()` khi WiFiManager captive portal
- Debug info WiFi + thời gian HH:MM:SS

**Báo cáo (3 trang):**
- Trang 1: FSM 4 trạng thái bệnh nhân + ngưỡng sinh hiệu
- Trang 2: Thiết kế buzzer (tần số, pattern, lý do phân cấp)
- Trang 3: Layout OLED, font, hiệu ứng nhấp nháy

**Demo (30 giây):** Trigger CRITICAL → buzzer 3kHz + OLED nhấp nháy

---

### 👤 TV5 — Mạng (WiFi/MQTT/Telegram + Phân quyền)

**Người phụ trách:** Vu Hoang Nam

**File phụ trách:**
- toàn bộ `src/network/`
- phần `wifiConnect()` trong `src/main.cpp`

**Công việc chi tiết (~240 LOC):**

#### 1. WiFi + WiFiManager (60 LOC)
- `wifiConnect()` trong main.cpp dùng WiFiManager captive portal
- AP name "Patient-Monitor-Setup", pass "12345678", timeout 180s
- `setAPCallback` hiển thị OLED hướng dẫn
- `autoConnect()`: thử NVS → fallback AP mode
- Fallback `WiFi.begin()` với hardcode SSID khi `USE_MOCK_DATA=1` (Wokwi)
- `wifiResetSettings()` cho trường hợp cần reset

#### 2. MQTT Client (60 LOC)
- `mqttSetup()`: kết nối HiveMQ broker:1883
- `mqttLoop()`: reconnect 3 lần, keepAlive 30s
- `publishPatient()`: publish 4 topic/BN (hr, spo2, state, fall)
- Topic format `hospital/ward1/patient/{id}/{field}`
- Helper `stateStr()` convert enum → string

#### 3. Telegram Bot + Phân quyền (90 LOC)
- `sendTelegramTo(chatId, msg)`: HTTPS POST tới api.telegram.org
- `WiFiClientSecure.setInsecure()` + `HTTPClient`
- `sendTelegram()` backward compat → route về bác sĩ
- `sendAlert(data)`: route theo phân quyền:
  - **Bác sĩ** (CHAT_ID_DOCTOR) nhận **tất cả**
  - **Người nhà** (FAMILY_CHAT_ID[patientId]) chỉ nhận BN tương ứng
- Cooldown 30s chống spam
- Escape dấu `"` trong message

#### 4. Xử lý lỗi + offline resilience (30 LOC)
- Kiểm tra `WiFi.status() != WL_CONNECTED` → skip gửi
- Log HTTP code (200/403/429...)
- Đảm bảo buzzer + OLED hoạt động offline

---

### 🔩 PHẦN NHÚNG LOW-LEVEL BỔ SUNG (để cân bằng độ nhúng)

#### 5. NVS Flash — quản lý persistent storage (30 LOC)
- Dùng thư viện `Preferences.h` (ESP32 NVS wrapper)
- Lưu `CHAT_ID_DOCTOR`, `CHAT_ID_FAMILY_BN1`, `CHAT_ID_FAMILY_BN2` vào flash
- Đọc lại khi boot → không cần flash firmware khi đổi chat ID
- API: `Preferences.begin("tg", false)`, `putString()`, `getString()`
- **Concept nhúng:** Non-Volatile Storage, flash partition, key-value store

#### 6. Watchdog Timer (WDT) (20 LOC)
- `esp_task_wdt_init(30, true)` — reset ESP32 nếu task treo > 30s
- `esp_task_wdt_add(xTaskGetCurrentTaskHandle())` trong taskNetwork
- `esp_task_wdt_reset()` mỗi chu kỳ → "feed" watchdog
- **Concept nhúng:** Hardware watchdog, auto-recovery khi firmware crash

#### 7. GPIO Interrupt — nút Reset WiFi (25 LOC)
- Gắn nút nhấn vào GPIO0 hoặc GPIO13 (có pull-up nội)
- `attachInterrupt(digitalPinToInterrupt(pin), onResetPressed, FALLING)`
- Hàm ISR (với `IRAM_ATTR`) — set flag `needReset = true`
- Main loop kiểm tra flag → xóa NVS WiFi → `ESP.restart()` → vào AP mode
- **Concept nhúng:** ISR, IRAM_ATTR, debouncing, flag-based async

#### 8. LED Status Driver (30 LOC)
- Điều khiển LED GPIO2 với pattern nháy theo trạng thái:
  - WiFi đang kết nối: nháy nhanh 4Hz
  - WiFi OK: sáng liên tục
  - Mất WiFi: nháy chậm 0.5Hz
  - MQTT connected: 1 nháy nhanh mỗi 2s
- Dùng hardware timer hoặc task riêng (priority 0)
- **Concept nhúng:** GPIO bit-banging, timer-based blinking, state machine LED

#### 9. OTA Update firmware qua WiFi (40 LOC) — *Điểm sáng tạo*
- `ArduinoOTA.begin()` → nhận firmware mới qua mạng, ghi flash partition B
- Progress callback hiển thị % trên OLED
- Password protect để chống nạp firmware lạ
- **Concept nhúng:** Bootloader, flash partition (ota_0/ota_1), firmware update cao cấp

**Tổng LOC bổ sung: +145 LOC** → Tổng TV5 = 385 LOC (cao hơn 4 người khác), nhưng độ nhúng tương đương.

**Báo cáo (3 trang):**
- Trang 1: WiFi captive portal + MQTT protocol
- Trang 2: Telegram Bot API + HTTPS handshake
- Trang 3: Sơ đồ phân quyền 3 role (doctor + 2 family)

**Demo (1 phút):** Trigger ngã BN#1 → bác sĩ + người nhà BN#1 nhận Telegram, người nhà BN#2 KHÔNG nhận

---

## 🔄 Luồng dữ liệu — ai dùng output của ai

```
          TV1 (MAX30102)     TV2 (MPU6050)
                │                 │
                └────────┬────────┘
                         ▼
                   PatientData
                   ax, ay, az, hr, spo2
                         │
                         ▼
                TV3 (main — FreeRTOS orchestration)
                         │
              ┌──────────┼──────────┐
              ▼                     ▼
        TV4 (alert + buzzer    TV5 (network)
              + OLED)
```

**Interface chung:** struct `PatientData` trong `config.h` — cả nhóm thống nhất trước khi bắt đầu.

---

## ⏱ Timeline 4 tuần

| Tuần | TV1 | TV2 | TV3 | TV4 | TV5 |
|------|-----|-----|-----|-----|-----|
| **Tuần 1** | Driver MAX30102 + Beat detector | Driver MPU + read accel | config.h struct + thiết kế task | Alert FSM (logic check threshold) | WiFi + MQTT cơ bản |
| **Tuần 2** | Thuật toán HR + SpO2 + filter | Fall FSM + SMA | Tạo task + mutex | Buzzer + buildAlertMessage | Telegram base (1 chat_id) |
| **Tuần 3** | Test MAX thật + tinh chỉnh ngưỡng | Test MPU thật + tinh chỉnh fall | Tích hợp toàn bộ task | OLED layout + hiệu ứng | Phân quyền Telegram |
| **Tuần 4** | Fix bug + báo cáo | Fix bug + báo cáo | Fix bug + báo cáo | Fix bug + báo cáo | Fix bug + báo cáo |

**Tuần 1-2:** TV1/TV2/TV4/TV5 làm song song độc lập. TV3 viết skeleton + interface.

**Tuần 3:** TV3 tích hợp + test end-to-end. TV1-5 fix theo feedback.

**Tuần 4:** polish + viết báo cáo + demo tập dợt.

---

## 🎤 Chia thuyết trình (12-15 phút)

| TV | Nội dung | Thời gian |
|----|----------|-----------|
| **TV3** (leader) | Giới thiệu đề tài + kiến trúc tổng thể | 2.5 phút |
| **TV1** | Driver MAX30102 + HR/SpO2 | 2 phút |
| **TV2** | Driver MPU + phát hiện ngã | 2 phút |
| **TV4** | Alert FSM + buzzer + OLED | 2 phút |
| **TV5** | WiFi + MQTT + Telegram phân quyền | 2 phút |
| Cả nhóm | Demo 5-7 phút + Q&A | 2-3 phút |

---

## 🎬 Kịch bản demo 5-7 phút (mỗi TV điều phối 1 phân đoạn)

1. **TV3 (1 phút):** khởi động → OLED loading → WiFiManager AP → WiFi OK
2. **TV1 (1 phút):** đặt ngón tay vào MAX → HR/SpO2 hiện
3. **TV2 (1 phút):** ném MPU xuống gối → FALL detected
4. **TV4 (30 giây):** buzzer 4kHz liên tục + OLED nhấp nháy
5. **TV5 (1 phút):** Telegram đến bác sĩ + người nhà BN#1, không đến BN#2
6. **Cả nhóm (1-2 phút):** Q&A + kết luận

---

## 📌 Tóm tắt cân bằng công việc

| Yếu tố | TV1 | TV2 | TV3 | TV4 | TV5 |
|--------|-----|-----|-----|-----|-----|
| LOC | 240 | 240 | 240 | 240 | 240 |
| Driver hardware | MAX30102 | MPU6050 | — | OLED | — |
| FSM | Beat detector | Fall FSM | — | Alert FSM | — |
| Thuật toán | HR+SpO2 | SMA | — | — | — |
| Real-time | — | — | FreeRTOS | Buzzer timing | MQTT timing |
| Network | — | — | — | — | WiFi+MQTT+TG |
| UI | — | — | — | OLED | — |
| Báo cáo | 3 trang | 3 trang | 3 trang | 3 trang | 3 trang |
| Thuyết trình | 2 phút | 2 phút | 2.5 phút | 2 phút | 2 phút |
| Demo | 1 phút | 1 phút | 1 phút | 30s | 1 phút |

**Mỗi người có:**
- ✅ 1 driver hardware (MAX / MPU / Task / OLED / WiFi)
- ✅ 1 thuật toán hoặc FSM riêng
- ✅ 1 phần giao tiếp/tương tác (I2C / tick timer / GPIO / MQTT)
- ✅ Phần báo cáo + thuyết trình + demo đều nhau

---

## 🤝 Nguyên tắc phối hợp

1. **Thống nhất interface trước code** — TV3 làm chủ `PatientData` + các ngưỡng trong `config.h`. Cả nhóm review trước khi push.

2. **Test driver độc lập** — TV1/TV2 dùng `src/test_bn1.cpp` test riêng trước khi tích hợp.

3. **Mock data** — khi chưa có phần cứng, `USE_MOCK_DATA=1` để TV3/TV4/TV5 không bị block.

4. **Git branch riêng** — mỗi TV 1 branch, merge PR khi module pass test:
   ```
   main
   ├── dev/max30102          (TV1)
   ├── dev/mpu-fall          (TV2)
   ├── dev/rtos              (TV3)
   ├── dev/alert-oled        (TV4)
   └── dev/network-telegram  (TV5)
   ```

5. **Daily sync 15 phút** — cả nhóm Zoom/Discord update tiến độ.

6. **Mỗi người hiểu ít nhất 2 module liền kề** để trả lời khi thầy hỏi ngẫu nhiên.

7. **Không ai sửa file của người khác** mà không thông báo → dùng Git PR.

---

## 📚 Kiến thức cần chuẩn bị cho từng TV

| TV | Kiến thức cần có | Tài liệu tham khảo |
|----|-------------------|-------------------|
| **TV1** | PPG principle, FIR filter, I2C, peak detection | MAX30102 datasheet, SparkFun MAX3010x library |
| **TV2** | IMU sensor, MEMS accelerometer, FSM design, I2C register-level | MPU-6050 datasheet, InvenSense register map |
| **TV3** | FreeRTOS (task/semaphore/queue), dual-core ESP32, real-time scheduling | Richard Barry — Mastering FreeRTOS |
| **TV4** | GPIO, PWM timer, U8g2 library, FSM threshold logic | U8g2 reference, ESP32 `tone()` doc |
| **TV5** | HTTP/HTTPS, MQTT protocol, WiFi stack ESP32, Telegram Bot API | PubSubClient doc, Telegram Bot API doc |

---

## 🎯 Tiêu chí đánh giá (thang 10đ)

Tham khảo đề cương môn để biết mỗi mục chiếm bao nhiêu điểm:

| Mục | Điểm | TV chính chịu trách nhiệm |
|-----|------|---------------------------|
| 1. Bài toán & yêu cầu hệ thống | 1.5đ | Cả nhóm (TV3 leader) |
| 2. Thiết kế hệ thống | 2.0đ | TV3 (kiến trúc) + TV4 (UI) |
| 3. Triển khai & lập trình | 2.5đ | TV1 + TV2 + TV5 (nhiều code nhất) |
| 4. Đánh giá & kiểm thử | 1.5đ | TV3 (tích hợp) + TV1-5 (test module) |
| 5. Tính sáng tạo & mở rộng | 1.5đ | Cả nhóm (mỗi TV 1-2 điểm sáng tạo) |
| 6. Báo cáo & trình bày | 1.0đ | Cả nhóm (chia đều) |

---

## ✍️ Ký xác nhận

Các thành viên đã đọc, thống nhất và cam kết hoàn thành đúng phần việc:

| Thành viên | Module phụ trách | Chữ ký |
|-----------|------------------|--------|
| Đàm Anh Đức — B22DCCN219 | TV__ : _____________________ | __________ |
| Lê Phương Nam — B22DCCN555 | TV__ : _____________________ | __________ |
| Vũ Hoàng Nam — B22DCCN567 | TV__ : _____________________ | __________ |
| Nguyễn Văn Nhất — B22DCCN579 | TV__ : _____________________ | __________ |
| Nguyễn Việt Quang — B22DCCN651 | TV__ : _____________________ | __________ |

*(Nhóm trưởng tự điền số TV tương ứng cho từng thành viên sau khi thống nhất)*
