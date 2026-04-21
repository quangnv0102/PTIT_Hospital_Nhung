#include "sensor.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Kênh TCA9548A (chỉ dùng khi USE_TCA9548A=1)
static const uint8_t MAX30102_CH[MAX_PATIENTS] = {
    CH_MAX30102_P0, CH_MAX30102_P1, CH_MAX30102_P2, CH_MAX30102_P3
};
static const uint8_t MPU6050_CH[MAX_PATIENTS] = {
    CH_MPU6050_P0, CH_MPU6050_P1, CH_MPU6050_P2, CH_MPU6050_P3
};

// ─── Thư viện phần cứng thật ──────────────────────────────────────────────
#if !USE_MOCK_DATA
#include <MAX30105.h>

// Mỗi bệnh nhân có 1 sensor object
static MAX30105 particleSensor[MAX_PATIENTS];
static bool     maxInited[MAX_PATIENTS] = {false};
static bool     mpuInited[MAX_PATIENTS] = {false};
static uint8_t  mpuAddr[MAX_PATIENTS]   = {0x68, 0x68, 0x68, 0x68};  // auto-detect khi init

// Bus I2C riêng cho từng BN khi không dùng TCA9548A
//   BN#1 → Wire  (GPIO21/22)
//   BN#2 → Wire1 (GPIO16/17)
static TwoWire* wireBus[MAX_PATIENTS] = {&Wire, &Wire1, &Wire, &Wire1};

// ── MPU-6050 / MPU-6500 raw I2C (bypass Adafruit — ho tro clone) ─────────
// Adafruit_MPU6050 check cung WHO_AM_I=0x68 → reject MPU-6500 (0x70)/clone.
// Dung raw I2C để hoạt động được với nhiều phiên bản MPU.
#define MPU_REG_PWR_MGMT_1   0x6B
#define MPU_REG_ACCEL_CONFIG 0x1C
#define MPU_REG_ACCEL_XOUT_H 0x3B
#define MPU_REG_WHO_AM_I     0x75
// ACCEL_FS_SEL: 0x00=±2g (16384 LSB/g) — phù hợp nhất cho fall detection
#define MPU_RANGE_CFG        0x00
#define MPU_SENSITIVITY      16384.0f

static uint8_t mpuReadReg(TwoWire* bus, uint8_t addr, uint8_t reg) {
    bus->beginTransmission(addr);
    bus->write(reg);
    if (bus->endTransmission(false) != 0) return 0xFF;
    if (bus->requestFrom((int)addr, 1) != 1) return 0xFF;
    return bus->read();
}

static bool mpuWriteReg(TwoWire* bus, uint8_t addr, uint8_t reg, uint8_t val) {
    bus->beginTransmission(addr);
    bus->write(reg);
    bus->write(val);
    return (bus->endTransmission() == 0);
}

// Thử init MPU ở địa chỉ addr. Trả về true nếu OK.
static bool mpuTryInit(TwoWire* bus, uint8_t addr) {
    uint8_t who = mpuReadReg(bus, addr, MPU_REG_WHO_AM_I);
    if (who == 0xFF) return false;                 // không có thiết bị
    // WHO_AM_I hợp lệ: 0x68 (MPU6050), 0x70 (MPU6500), 0x71/72/73 (MPU9250/9255), 0x98 (clone)
    if (!mpuWriteReg(bus, addr, MPU_REG_PWR_MGMT_1, 0x00)) return false; // wake
    delay(100);
    mpuWriteReg(bus, addr, MPU_REG_ACCEL_CONFIG, MPU_RANGE_CFG);
    Serial.printf("  MPU WHO_AM_I=0x%02X tai 0x%02X\n", who, addr);
    return true;
}

// Đọc gia tốc raw rồi quy đổi về đơn vị g
static bool mpuReadAccel(TwoWire* bus, uint8_t addr,
                          float* ax, float* ay, float* az) {
    bus->beginTransmission(addr);
    bus->write(MPU_REG_ACCEL_XOUT_H);
    if (bus->endTransmission(false) != 0) { *ax=*ay=*az=0; return false; }
    if (bus->requestFrom((int)addr, 6) != 6) { *ax=*ay=*az=0; return false; }
    int16_t rx = (bus->read() << 8) | bus->read();
    int16_t ry = (bus->read() << 8) | bus->read();
    int16_t rz = (bus->read() << 8) | bus->read();
    *ax = rx / MPU_SENSITIVITY;
    *ay = ry / MPU_SENSITIVITY;
    *az = rz / MPU_SENSITIVITY;
    return true;
}

// ── Beat Detector PER-PATIENT (thay cho checkForBeat global của SparkFun) ──
// Thuật toán chuyển từ heartRate.cpp của SparkFun, nhưng state đã được
// đóng gói vào struct → an toàn khi dùng cho nhiều bệnh nhân.
struct BeatDetector {
    int16_t IR_AC_Max;
    int16_t IR_AC_Min;
    int16_t IR_AC_Signal_Current;
    int16_t IR_AC_Signal_Previous;
    int16_t IR_AC_Signal_min;
    int16_t IR_AC_Signal_max;
    int16_t IR_Average_Estimated;
    int16_t positiveEdge;
    int16_t negativeEdge;
    int32_t ir_avg_reg;
    int16_t cbuf[32];
    uint8_t offset;
};
static BeatDetector beatDet[MAX_PATIENTS] = {};

// FIR filter coefficients (sao chép từ SparkFun heartRate.cpp)
static const uint16_t FIRCoeffs[12] = {
    172, 321, 579, 927, 1360, 1858,
    2390, 2916, 3391, 3768, 4012, 4096
};

static int16_t bd_averageDC(int32_t* p, uint16_t x) {
    *p += ((((long)x << 15) - *p) >> 4);
    return (*p >> 15);
}

static int16_t bd_lowPassFIR(BeatDetector* bd, int16_t din) {
    bd->cbuf[bd->offset] = din;
    int32_t z = (int32_t)FIRCoeffs[11] * (int32_t)bd->cbuf[(bd->offset - 11) & 0x1F];
    for (uint8_t i = 0; i < 11; i++) {
        z += (int32_t)FIRCoeffs[i] *
             (int32_t)(bd->cbuf[(bd->offset - i) & 0x1F]
                     + bd->cbuf[(bd->offset - 22 + i) & 0x1F]);
    }
    bd->offset = (bd->offset + 1) % 32;
    return (z >> 15);
}

static bool bd_checkForBeat(int patientId, int32_t sample) {
    BeatDetector* bd = &beatDet[patientId];
    bool beat = false;

    bd->IR_AC_Signal_Previous = bd->IR_AC_Signal_Current;
    bd->IR_Average_Estimated  = bd_averageDC(&bd->ir_avg_reg, sample);
    bd->IR_AC_Signal_Current  = bd_lowPassFIR(bd, sample - bd->IR_Average_Estimated);

    // Phát hiện rising edge (qua 0 từ âm sang dương) → 1 nhịp
    if ((bd->IR_AC_Signal_Previous < 0) & (bd->IR_AC_Signal_Current >= 0)) {
        bd->IR_AC_Max = bd->IR_AC_Signal_max;
        bd->IR_AC_Min = bd->IR_AC_Signal_min;
        bd->positiveEdge = 1;
        bd->negativeEdge = 0;
        bd->IR_AC_Signal_max = 0;
        if ((bd->IR_AC_Max - bd->IR_AC_Min) > 20
         && (bd->IR_AC_Max - bd->IR_AC_Min) < 1000) {
            beat = true;
        }
    }
    // Falling edge
    if ((bd->IR_AC_Signal_Previous > 0) & (bd->IR_AC_Signal_Current <= 0)) {
        bd->positiveEdge = 0;
        bd->negativeEdge = 1;
        bd->IR_AC_Signal_min = 0;
    }
    // Tìm max trong nửa chu kỳ dương
    if (bd->positiveEdge & (bd->IR_AC_Signal_Current > bd->IR_AC_Signal_Previous)) {
        bd->IR_AC_Signal_max = bd->IR_AC_Signal_Current;
    }
    // Tìm min trong nửa chu kỳ âm
    if (bd->negativeEdge & (bd->IR_AC_Signal_Current < bd->IR_AC_Signal_Previous)) {
        bd->IR_AC_Signal_min = bd->IR_AC_Signal_Current;
    }
    return beat;
}

// ── HR state per-patient ──────────────────────────────────────────────────
#define RATE_SIZE  4
static float    rates[MAX_PATIENTS][RATE_SIZE];
static int      rateIdx[MAX_PATIENTS]      = {0};
static long     lastBeatTime[MAX_PATIENTS] = {0};
static float    lastValidHr[MAX_PATIENTS]  = {0.0f};

// ── SpO2 state per-patient ────────────────────────────────────────────────
static long     irMax[MAX_PATIENTS],  irMin[MAX_PATIENTS];
static long     redMax[MAX_PATIENTS], redMin[MAX_PATIENTS];
static float    lastValidSpo2[MAX_PATIENTS] = {0.0f};
static bool     spo2Reset[MAX_PATIENTS]     = {true, true, true, true};
#endif

// ─── Khởi tạo toàn bộ sensor ──────────────────────────────────────────────
void sensorSetup() {
    Wire.begin (PIN_SDA0,   PIN_SCL0);     // Bus 0 cho BN#1
#if !USE_MOCK_DATA
    Wire1.begin(PIN_SDA_P1, PIN_SCL_P1);   // Bus 1 cho BN#2

    // ── I2C Scanner cho cả 2 bus ─────────────────────────────────────────
    auto scanBus = [](TwoWire& w, const char* name, int sda, int scl) {
        Serial.printf("[I2C Scan] %s (GPIO%d/GPIO%d):\n", name, sda, scl);
        int found = 0;
        for (uint8_t addr = 1; addr < 127; addr++) {
            w.beginTransmission(addr);
            if (w.endTransmission() == 0) {
                Serial.printf("  -> 0x%02X", addr);
                if (addr == 0x57) Serial.print(" (MAX30102)");
                if (addr == 0x68) Serial.print(" (MPU6050)");
                Serial.println();
                found++;
            }
        }
        if (found == 0) Serial.println("  Khong tim thay thiet bi nao!");
    };
    scanBus(Wire,  "Bus 0", PIN_SDA0,   PIN_SCL0);
    scanBus(Wire1, "Bus 1", PIN_SDA_P1, PIN_SCL_P1);

    // ── Khởi tạo từng bệnh nhân ──────────────────────────────────────────
    for (int i = 0; i < NUM_PATIENTS; i++) {
#if USE_TCA9548A
        selectChannel(MAX30102_CH[i]);
#endif
        // MAX30102 — dùng wireBus[i] (Wire cho BN#1, Wire1 cho BN#2)
        if (particleSensor[i].begin(*wireBus[i], I2C_SPEED_STANDARD)) {
            particleSensor[i].setup(0x20, 4, 2, 100, 411, 16384);
            maxInited[i] = true;
            Serial.printf("[Sensor] MAX30102 BN#%d OK\n", i + 1);
        } else {
            Serial.printf("[Sensor] MAX30102 BN#%d FAIL\n", i + 1);
        }

#if USE_TCA9548A
        selectChannel(MPU6050_CH[i]);
#endif
        // MPU — thử 0x68 trước, fallback 0x69 nếu AD0 nối VCC
        if (mpuTryInit(wireBus[i], 0x68)) {
            mpuAddr[i] = 0x68; mpuInited[i] = true;
            Serial.printf("[Sensor] MPU BN#%d OK tai 0x68\n", i + 1);
        } else if (mpuTryInit(wireBus[i], 0x69)) {
            mpuAddr[i] = 0x69; mpuInited[i] = true;
            Serial.printf("[Sensor] MPU BN#%d OK tai 0x69\n", i + 1);
        } else {
            Serial.printf("[Sensor] MPU BN#%d FAIL (check AD0-GND, day SDA/SCL)\n", i + 1);
        }
    }
    delay(3000);
#else
    Serial.println("[Sensor] USE_MOCK_DATA=1 — bo qua khoi tao phan cung");
#endif
}

// ─── TCA9548A channel select ──────────────────────────────────────────────
void selectChannel(uint8_t channel) {
    if (channel > 7) return;
    Wire.beginTransmission(ADDR_TCA9548A);
    Wire.write(1 << channel);
    Wire.endTransmission();
}

bool isDeviceConnected(uint8_t addr) {
    Wire.beginTransmission(addr);
    return (Wire.endTransmission() == 0);
}

// ─── Đọc MAX30102 ─────────────────────────────────────────────────────────
void readMAX30102(int patientId, float* hr, float* spo2) {
#if USE_MOCK_DATA
    static float mockHr[MAX_PATIENTS]   = {72.0f, 68.0f, 75.0f, 70.0f};
    static float mockSpo2[MAX_PATIENTS] = {97.0f, 98.0f, 96.0f, 99.0f};
    if (random(10) < 3) {
        mockHr[patientId] += (float)(random(-5, 6)) * 0.2f;
        mockHr[patientId]  = constrain(mockHr[patientId], 45.0f, 130.0f);
    }
    if (random(10) < 2) {
        mockSpo2[patientId] += (float)(random(-3, 4)) * 0.1f;
        mockSpo2[patientId]  = constrain(mockSpo2[patientId], 84.0f, 100.0f);
    }
    *hr   = mockHr[patientId];
    *spo2 = mockSpo2[patientId];
#else
#if USE_TCA9548A
    selectChannel(MAX30102_CH[patientId]);
#endif
    if (!maxInited[patientId]) {
        *hr = 0.0f; *spo2 = 0.0f;
        return;
    }

    // Quick check ngón tay
    long irCheck = particleSensor[patientId].getIR();
    if (irCheck < 7000) {
        *hr   = 0.0f;
        *spo2 = 0.0f;
        return;
    }

    if (spo2Reset[patientId]) {
        irMax[patientId]  = 0; irMin[patientId]  = 100000;
        redMax[patientId] = 0; redMin[patientId] = 100000;
        spo2Reset[patientId] = false;
    }

    particleSensor[patientId].check();
    while (particleSensor[patientId].available()) {
        uint32_t irValue  = particleSensor[patientId].getFIFOIR();
        uint32_t redValue = particleSensor[patientId].getFIFORed();
        particleSensor[patientId].nextSample();

        if (irValue < 7000) continue;

        // Cập nhật min/max cho SpO2
        if ((long)irValue  > irMax[patientId])  irMax[patientId]  = irValue;
        if ((long)irValue  < irMin[patientId])  irMin[patientId]  = irValue;
        if ((long)redValue > redMax[patientId]) redMax[patientId] = redValue;
        if ((long)redValue < redMin[patientId]) redMin[patientId] = redValue;

        // ── HR: dùng beat detector PER-PATIENT (an toàn cho 2 BN) ──────
        if (bd_checkForBeat(patientId, irValue)) {
            long now   = millis();
            long delta = now - lastBeatTime[patientId];
            lastBeatTime[patientId] = now;

            if (delta > 300 && delta < 2000) {
                float bpm = 60000.0f / (float)delta;
                rates[patientId][rateIdx[patientId] % RATE_SIZE] = bpm;
                rateIdx[patientId]++;
                int count = rateIdx[patientId] < RATE_SIZE
                            ? rateIdx[patientId] : RATE_SIZE;
                float sum = 0;
                for (int i = 0; i < count; i++) sum += rates[patientId][i];
                lastValidHr[patientId] = sum / (float)count;
            }

            // ── SpO2: tính tại mỗi nhịp ────────────────────────────────
            float acRed = (float)(redMax[patientId] - redMin[patientId]);
            float dcRed = (float)(redMax[patientId] + redMin[patientId]) / 2.0f;
            float acIR  = (float)(irMax[patientId]  - irMin[patientId]);
            float dcIR  = (float)(irMax[patientId]  + irMin[patientId])  / 2.0f;

            if (dcRed > 0 && dcIR > 0 && acIR > 0) {
                float R = (acRed / dcRed) / (acIR / dcIR);
                float spo2Est = 110.0f - 25.0f * R;
                spo2Est = constrain(spo2Est, 70.0f, 100.0f);
                if (lastValidSpo2[patientId] < 1.0f) {
                    lastValidSpo2[patientId] = spo2Est;
                } else {
                    lastValidSpo2[patientId] = lastValidSpo2[patientId] * 0.7f
                                             + spo2Est * 0.3f;
                }
            }
            irMax[patientId]  = 0; irMin[patientId]  = 100000;
            redMax[patientId] = 0; redMin[patientId] = 100000;
        }
    }
    *hr   = lastValidHr[patientId];
    *spo2 = lastValidSpo2[patientId];
#endif
}

// ─── Đọc MPU6050 ──────────────────────────────────────────────────────────
void readMPU6050(int patientId, float* ax, float* ay, float* az) {
#if USE_MOCK_DATA
    *ax = (float)(random(-30, 31)) * 0.01f;
    *ay = (float)(random(-30, 31)) * 0.01f;
    *az = 1.0f + (float)(random(-20, 21)) * 0.01f;
#else
#if USE_TCA9548A
    selectChannel(MPU6050_CH[patientId]);
#endif
    if (!mpuInited[patientId]) {
        *ax = 0.0f; *ay = 0.0f; *az = 1.0f;
        return;
    }
    mpuReadAccel(wireBus[patientId], mpuAddr[patientId], ax, ay, az);
#endif
}

// ─── Đọc toàn bộ dữ liệu bệnh nhân ───────────────────────────────────────
void readPatient(int patientId, PatientData* data) {
#if !USE_MOCK_DATA
#if USE_TCA9548A
    selectChannel(MAX30102_CH[patientId]);
    data->sensorConnected = isDeviceConnected(ADDR_MAX30102);
#else
    // Không có TCA → check trên bus tương ứng của BN
    wireBus[patientId]->beginTransmission(ADDR_MAX30102);
    data->sensorConnected = (wireBus[patientId]->endTransmission() == 0);
#endif
#else
    data->sensorConnected = true;
#endif

    readMAX30102(patientId, &data->hr,   &data->spo2);
    readMPU6050 (patientId, &data->ax,   &data->ay, &data->az);
    data->lastUpdate = millis();
}
