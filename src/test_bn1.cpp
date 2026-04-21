// ============================================================================
// TEST FILE — Benh nhan #1: MAX30102 + MPU6050 + OLED + WiFi + Telegram
// ============================================================================
// Chay voi: [env:test_bn1] trong platformio.ini
//
// Thu tu init thong minh:
//   1. I2C scan -> biet san device nao co tren bus
//   2. Init MPU TRUOC (don gian, it loi) -> xac nhan bus OK
//   3. Neu scan thay 0x57 -> init MAX; neu khong -> skip MAX
//   4. Trong loop: neu MAX gap Error -1 >5 lan lien tiep -> auto-disable
//      tranh spam va tranh keo chet MPU
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <MAX30105.h>
#include <heartRate.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"

// ---- Pin Bus 0 (BN#1) ----
#define PIN_SDA   21
#define PIN_SCL   22

// ---- Flag bypass MAX khi can test rieng MPU ----
// 0 = test binh thuong ca MAX + MPU (mac dinh)
// 1 = bo qua MAX hoan toan (khi MAX chet/lung)
#define SKIP_MAX  0

// ---- Nguong canh bao ----
#define IMPACT_G      2.5f
#define STILL_G       1.2f
#define FALL_WINDOW   1000UL
#define HR_HIGH       120.0f
#define HR_LOW        50.0f
#define SPO2_LOW      90.0f
#define TG_COOLDOWN   30000UL

// ---- Auto-disable MAX khi loi lien tiep ----
#define MAX_ERR_THRESHOLD  10

// ---- Sensor objects ----
MAX30105 maxSensor;

// ---- OLED (SW_I2C GPIO18=SDA, GPIO19=SCL theo config.h) ----
static U8G2_SH1106_128X64_NONAME_F_SW_I2C
    oled(U8G2_R0, PIN_SCL1, PIN_SDA1, U8X8_PIN_NONE);

bool     maxOK = false, mpuOK = false;
bool     maxPresent = false;    // co 0x57 tren bus khong
uint8_t  mpuAddr = 0;
int      maxErrCount = 0;       // dem loi lien tiep cua MAX

// ---- HR/SpO2 state ----
const uint8_t RATE_SIZE = 4;
float   rates[RATE_SIZE] = {0};
uint8_t rateSpot = 0;
long    lastBeat = 0;
int     beatAvg = 0;
long    irMax = 0, irMin = 100000, redMax = 0, redMin = 100000;
float   spo2Smooth = 0;

// ---- Custom beat detector cho module IR thap ----
float   irDC = 0;             // DC baseline (slow tracker)
float   prevAC = 0, prevPrevAC = 0;
float   acMax = 0;            // de hien thi debug
long    acRange = 0;          // bien do AC tren 1 nhip

// Tra ve true khi phat hien dinh (peak) cua nhip
bool detectBeat(long ir) {
    if (irDC < 1.0f) irDC = ir;           // khoi tao lan dau
    irDC = irDC * 0.995f + ir * 0.005f;   // DC tracker (tau ~200 sample)
    float ac = (float)ir - irDC;

    // Peak detection: prev la dinh neu > ca truoc va sau, va vuot threshold
    bool beat = (prevAC > ac) && (prevAC > prevPrevAC) && (prevAC > 20.0f);

    if (ac > acMax) acMax = ac;
    prevPrevAC = prevAC;
    prevAC = ac;
    return beat;
}

// ---- Fall FSM ----
enum FallState { FIDLE, FIMPACT };
FallState fallState = FIDLE;
unsigned long impactTime = 0;
unsigned long lastTelegram = 0;

// ============================================================================
// MPU raw I2C (bypass Adafruit — ho tro ca MPU-6050/6500/9250)
// ============================================================================
#define MPU_PWR_MGMT_1   0x6B
#define MPU_ACCEL_CONFIG 0x1C
#define MPU_ACCEL_XOUT_H 0x3B
#define MPU_WHO_AM_I     0x75

uint8_t mpuReadReg(uint8_t addr, uint8_t reg) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return 0xFF;
    if (Wire.requestFrom((int)addr, 1) != 1) return 0xFF;
    return Wire.read();
}

bool mpuWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

// ACCEL_FS_SEL: 0x00=±2g (16384 LSB/g), 0x08=±4g (8192), 0x10=±8g (4096), 0x18=±16g (2048)
#define MPU_RANGE_CFG    0x00
#define MPU_SENSITIVITY  16384.0f

bool mpuInit(uint8_t addr) {
    if (!mpuWriteReg(addr, MPU_PWR_MGMT_1, 0x00)) return false;   // wake up
    delay(100);
    mpuWriteReg(addr, MPU_ACCEL_CONFIG, MPU_RANGE_CFG);
    return true;
}

bool mpuReadAccel(uint8_t addr, float* ax, float* ay, float* az) {
    Wire.beginTransmission(addr);
    Wire.write(MPU_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) { *ax=*ay=*az=0; return false; }
    if (Wire.requestFrom((int)addr, 6) != 6)  { *ax=*ay=*az=0; return false; }
    int16_t rx = (Wire.read() << 8) | Wire.read();
    int16_t ry = (Wire.read() << 8) | Wire.read();
    int16_t rz = (Wire.read() << 8) | Wire.read();
    *ax = rx / MPU_SENSITIVITY;
    *ay = ry / MPU_SENSITIVITY;
    *az = rz / MPU_SENSITIVITY;
    return true;
}

// ============================================================================
// I2C bus recovery — toggle SCL 9 lan de giai phong SDA bi stuck
// ============================================================================
void i2cBusRecovery() {
    Serial.println("[I2C] Bus recovery — toggle SCL 9 lan...");
    Wire.end();
    pinMode(PIN_SDA, INPUT_PULLUP);
    pinMode(PIN_SCL, OUTPUT);
    for (int i = 0; i < 9; i++) {
        digitalWrite(PIN_SCL, HIGH); delayMicroseconds(5);
        digitalWrite(PIN_SCL, LOW);  delayMicroseconds(5);
    }
    digitalWrite(PIN_SCL, HIGH);
    delay(10);
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);
}

// ============================================================================
// I2C scan — tra ve bitmask: bit0=MAX (0x57), bit1=MPU68, bit2=MPU69
// ============================================================================
uint8_t i2cScan() {
    Serial.println("\n[I2C Scan] GPIO21(SDA) / GPIO22(SCL):");
    uint8_t mask = 0;
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  -> 0x%02X", a);
            if (a == 0x57) { Serial.print("  (MAX30102)");         mask |= 0x01; }
            if (a == 0x68) { Serial.print("  (MPU6050 ADDR=GND)"); mask |= 0x02; }
            if (a == 0x69) { Serial.print("  (MPU6050 ADDR=VCC)"); mask |= 0x04; }
            Serial.println();
            found++;
        }
    }
    if (!found) Serial.println("  !! KHONG THAY THIET BI NAO !!");
    Serial.printf("  Tong: %d thiet bi\n\n", found);
    return mask;
}

// ============================================================================
// Telegram
// ============================================================================
bool sendTelegram(const String& msg) {
    if (WiFi.status() != WL_CONNECTED) return false;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(BOT_TOKEN) + "/sendMessage";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    String body = "{\"chat_id\":\"" + String(CHAT_ID) +
                  "\",\"text\":\"" + msg + "\"}";
    int code = http.POST(body);
    http.end();
    Serial.printf("[TG] HTTP %d: %s\n", code, msg.c_str());
    return (code == 200);
}

void tryAlert(const String& msg) {
    unsigned long now = millis();
    if (now - lastTelegram < TG_COOLDOWN) return;
    if (sendTelegram(msg)) lastTelegram = now;
}

// ============================================================================
// OLED
// ============================================================================
void drawOLED(bool finger, int hr, int spo2,
              float ax, float ay, float az, float sma,
              const char* alertText) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);

    oled.drawStr(0, 8, "TEST BN#1");
    oled.drawHLine(0, 10, 128);

    char buf[32];
    snprintf(buf, sizeof(buf), "MAX:%s MPU:%s",
             maxOK ? "OK" : "FAIL", mpuOK ? "OK" : "FAIL");
    oled.drawStr(0, 20, buf);

    if (maxOK && finger) {
        snprintf(buf, sizeof(buf), "HR:%d SpO2:%d%%", hr, spo2);
    } else if (maxOK) {
        snprintf(buf, sizeof(buf), "Cham ngon tay!");
    } else {
        snprintf(buf, sizeof(buf), "MAX khong hoat dong");
    }
    oled.drawStr(0, 30, buf);

    snprintf(buf, sizeof(buf), "ax%+.1f ay%+.1f az%+.1f", ax, ay, az);
    oled.drawStr(0, 40, buf);
    snprintf(buf, sizeof(buf), "SMA:%.2fg", sma);
    oled.drawStr(0, 50, buf);

    oled.drawHLine(0, 52, 128);
    if (alertText && alertText[0]) {
        oled.drawStr(0, 62, alertText);
    } else {
        snprintf(buf, sizeof(buf), "WiFi:%s",
                 WiFi.status() == WL_CONNECTED ? "OK" : "--");
        oled.drawStr(0, 62, buf);
    }
    oled.sendBuffer();
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== TEST BN#1 (MAX + MPU + OLED + Telegram) ===");

    // OLED
    oled.begin();
    oled.setFont(u8g2_font_6x10_tr);
    oled.clearBuffer();
    oled.drawStr(10, 30, "TEST BN#1 init...");
    oled.sendBuffer();

    // I2C Bus 0
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(100000);
    delay(100);

    uint8_t mask = i2cScan();
    maxPresent = (mask & 0x01);
    bool mpu68 = (mask & 0x02);
    bool mpu69 = (mask & 0x04);

    // ---- Init MPU TRUOC (it kha nang loi hon) ----
    if (mpu68 || mpu69) {
        uint8_t tryAddr = mpu68 ? 0x68 : 0x69;
        uint8_t who = mpuReadReg(tryAddr, MPU_WHO_AM_I);
        Serial.printf("[MPU] WHO_AM_I tai 0x%02X = 0x%02X  ", tryAddr, who);
        if (mpuInit(tryAddr)) {
            mpuOK = true;
            mpuAddr = tryAddr;
            const char* name = "Unknown";
            if (who == 0x68) name = "MPU-6050 chinh hang";
            else if (who == 0x70) name = "MPU-6500";
            else if (who == 0x71 || who == 0x73) name = "MPU-9250";
            else if (who == 0x72) name = "MPU-9255";
            else if (who == 0x98) name = "Clone TQ";
            Serial.printf("OK — %s\n", name);
        } else {
            Serial.println("init FAIL");
        }
    } else {
        Serial.println("[MPU] Khong thay 0x68 va 0x69 tren bus");
    }

    // ---- Init MAX SAU, chi khi scan thay 0x57 ----
#if SKIP_MAX
    Serial.println("[MAX30102] BI BO QUA (SKIP_MAX=1)");
#else
    if (!maxPresent) {
        Serial.println("[MAX30102] Scan khong thay 0x57 -> BO QUA");
        Serial.println("           Kiem tra: VIN=3V3? SDA/SCL dung? Han chan?");
    } else {
        Serial.println("[MAX30102] init...");
        // Thu 2 lan, co bus recovery o giua
        for (int attempt = 1; attempt <= 2; attempt++) {
            if (maxSensor.begin(Wire, I2C_SPEED_STANDARD)) {
                // Config can bang — tranh saturation:
                //   powerLevel=0x24 (LED vua phai, IR ngon tay ~50k-100k)
                //   sampleAverage=8 (giam noise), ledMode=2 (Red+IR)
                //   sampleRate=100Hz, pulseWidth=411us, adcRange=16384 (rong)
                maxSensor.setup(0x24, 8, 2, 100, 411, 16384);
                maxSensor.setPulseAmplitudeRed(0x24);
                maxSensor.setPulseAmplitudeIR(0x24);
                maxOK = true;
                Serial.printf("[MAX30102] OK (lan %d)\n", attempt);
                break;
            }
            Serial.printf("[MAX30102] FAIL lan %d\n", attempt);
            if (attempt < 2) i2cBusRecovery();
        }
    }
#endif

    // ---- WiFi ----
    oled.clearBuffer();
    oled.drawStr(0, 20, "Ket noi WiFi...");
    oled.drawStr(0, 35, "Neu chua co WiFi,");
    oled.drawStr(0, 45, "cam DT vao AP:");
    oled.drawStr(0, 58, "PatientMonitor-Test");
    oled.sendBuffer();

    WiFiManager wm;
    wm.setConfigPortalTimeout(120);
    wm.setConnectTimeout(15);
    if (wm.autoConnect("PatientMonitor-Test", "12345678")) {
        Serial.printf("[WiFi] OK — IP %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Khong ket noi duoc");
    }

    if (WiFi.status() == WL_CONNECTED) {
        sendTelegram("TEST BN#1 khoi dong — MAX:" + String(maxOK ? "OK" : "FAIL") +
                     " MPU:" + String(mpuOK ? "OK" : "FAIL"));
    }

    Serial.println("\n>>> Bat dau vong lap.");
    Serial.println(">>> Cham ngon tay vao MAX30102 de do HR/SpO2.");
    Serial.println(">>> Lac MPU manh roi de yen 1s de test phat hien nga.\n");
}

// ============================================================================
// Loop
// ============================================================================
unsigned long lastOled = 0, lastLog = 0;

void loop() {
    unsigned long now = millis();

    // ---- MAX30102 ----
    long ir = 0, red = 0;
    bool finger = false;
    if (maxOK) {
        ir  = maxSensor.getIR();
        red = maxSensor.getRed();
        // Phat hien bus loi: IR va Red cung = 0 lien tuc
        if (ir == 0 && red == 0) {
            maxErrCount++;
            if (maxErrCount >= MAX_ERR_THRESHOLD) {
                Serial.println("\n[MAX] Loi lien tiep -> AUTO DISABLE de bao ve MPU\n");
                maxOK = false;
                i2cBusRecovery();
            }
        } else {
            maxErrCount = 0;
            finger = (ir > 15000 && ir < 250000);    // finger nhung khong saturated

            if (finger) {
                if (ir > irMax)  irMax = ir;
                if (ir < irMin)  irMin = ir;
                if (red > redMax) redMax = red;
                if (red < redMin) redMin = red;

                if (detectBeat(ir)) {
                    long delta = now - lastBeat;
                    lastBeat = now;
                    if (delta > 300 && delta < 2000) {
                        float bpm = 60000.0f / delta;
                        rates[rateSpot++ % RATE_SIZE] = bpm;
                        float sum = 0;
                        int cnt = rateSpot < RATE_SIZE ? rateSpot : RATE_SIZE;
                        for (int i = 0; i < cnt; i++) sum += rates[i];
                        beatAvg = (int)(sum / cnt);
                        acRange = (long)acMax;
                        acMax = 0;     // reset cho nhip tiep theo
                    }
                    float acR = redMax - redMin, dcR = (redMax + redMin) / 2.0f;
                    float acI = irMax  - irMin,  dcI = (irMax + irMin) / 2.0f;
                    if (dcR > 0 && dcI > 0 && acI > 0) {
                        float R = (acR / dcR) / (acI / dcI);
                        float s = constrain(110.0f - 25.0f * R, 70.0f, 100.0f);
                        spo2Smooth = (spo2Smooth < 1.0f) ? s
                                     : (spo2Smooth * 0.7f + s * 0.3f);
                    }
                    irMax = 0; irMin = 100000; redMax = 0; redMin = 100000;
                }
            } else {
                beatAvg = 0; spo2Smooth = 0;
                irDC = 0; prevAC = prevPrevAC = 0; acMax = 0;   // reset beat detector
            }
        }
    }

    // ---- MPU ----
    float ax = 0, ay = 0, az = 0, sma = 0;
    if (mpuOK) {
        mpuReadAccel(mpuAddr, &ax, &ay, &az);
        sma = sqrtf(ax*ax + ay*ay + az*az);
    }

    // ---- Fall FSM ----
    const char* alertMsg = "";
    switch (fallState) {
        case FIDLE:
            if (mpuOK && sma > IMPACT_G) {
                fallState = FIMPACT;
                impactTime = now;
                Serial.println(">>> IMPACT! Cho bat dong...");
            }
            break;
        case FIMPACT:
            if (now - impactTime > FALL_WINDOW) {
                fallState = FIDLE;
                Serial.println(">>> False alarm, reset");
            } else if (sma < STILL_G) {
                fallState = FIDLE;
                alertMsg = "PHAT HIEN NGA!";
                Serial.println(">>> !!! FALL CONFIRMED !!!");
                tryAlert("PHAT HIEN NGA - BN#1 (TEST)\nSMA vuot 2.5g roi bat dong.");
            }
            break;
    }

    // ---- HR/SpO2 bat thuong ----
    if (maxOK && finger && beatAvg > 0) {
        if (beatAvg > HR_HIGH || beatAvg < HR_LOW) {
            alertMsg = "HR BAT THUONG";
            tryAlert("HR bat thuong - BN#1: " + String(beatAvg) + " BPM");
        } else if (spo2Smooth > 0 && spo2Smooth < SPO2_LOW) {
            alertMsg = "SpO2 THAP";
            tryAlert("SpO2 thap - BN#1: " + String((int)spo2Smooth) + "%");
        }
    }

    // ---- Log 200ms ----
    if (now - lastLog >= 200) {
        lastLog = now;
        Serial.printf("[MAX %s] IR=%6ld DC=%6ld AC=%4ld %s HR=%3d SpO2=%3d | "
                      "[MPU %s] ax=%+.2f ay=%+.2f az=%+.2f SMA=%.2f\n",
                      maxOK ? "OK " : "off", ir, (long)irDC, acRange,
                      finger ? "FING" : " -- ",
                      beatAvg, (int)spo2Smooth,
                      mpuOK ? "OK " : "off",
                      ax, ay, az, sma);
    }

    // ---- OLED 250ms ----
    if (now - lastOled >= 250) {
        lastOled = now;
        drawOLED(finger, beatAvg, (int)spo2Smooth, ax, ay, az, sma, alertMsg);
    }
}
