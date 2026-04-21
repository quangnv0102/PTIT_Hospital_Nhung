#include "telegram.h"
#include "../alert/alert.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static const char* TELEGRAM_HOST = "api.telegram.org";

// ─── Mang chat_id nguoi nha, index = patientId ─────────────────────────────
// BN#i → FAMILY_CHAT_ID[i]. Neu rong ("") -> khong gui cho nguoi nha BN do.
static const char* FAMILY_CHAT_ID[MAX_PATIENTS] = {
    CHAT_ID_FAMILY_BN1,   // BN#1 (patientId=0)
    CHAT_ID_FAMILY_BN2,   // BN#2 (patientId=1)
    "",                   // BN#3 (chua co)
    ""                    // BN#4 (chua co)
};

// ─── Gui tin den 1 chat_id cu the ──────────────────────────────────────────
bool sendTelegramTo(const char* chatId, const String& message) {
    if (!chatId || !chatId[0]) return false;              // skip neu rong
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Telegram] WiFi mat ket noi — bo qua gui tin");
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();   // skip verify cert (demo)

    HTTPClient http;
    String url = "https://" + String(TELEGRAM_HOST) +
                 "/bot" + String(BOT_TOKEN) + "/sendMessage";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    // Escape quotes trong message
    String safeMsg = message;
    safeMsg.replace("\"", "'");

    String body = "{\"chat_id\":\"" + String(chatId) +
                  "\",\"text\":\"" + safeMsg + "\"}";

    int httpCode = http.POST(body);
    http.end();

    if (httpCode == 200) {
        Serial.printf("[Telegram] -> %s: OK\n", chatId);
        return true;
    } else {
        Serial.printf("[Telegram] -> %s: loi HTTP %d\n", chatId, httpCode);
        return false;
    }
}

// ─── Backward compat: gui cho bac si ───────────────────────────────────────
bool sendTelegram(const String& message) {
    return sendTelegramTo(CHAT_ID_DOCTOR, message);
}

// ─── Gui canh bao theo phan quyen ──────────────────────────────────────────
void sendAlert(PatientData* data) {
    unsigned long now = millis();

    // Cooldown chung cho ca BN (khong gui lai trong 30s ke tu lan gan nhat)
    if ((now - data->lastAlertSent) < TELEGRAM_COOLDOWN_MS) return;

    String msg = buildAlertMessage(data);
    Serial.printf("[Alert] BN#%d -> gui canh bao:\n%s\n", data->id + 1, msg.c_str());

    bool anySuccess = false;

    // 1. Gui bac si — luon nhan canh bao cua moi BN
    if (sendTelegramTo(CHAT_ID_DOCTOR, msg)) {
        anySuccess = true;
    }

    // 2. Gui nguoi nha cua BN tuong ung (neu co cau hinh)
    if (data->id >= 0 && data->id < MAX_PATIENTS) {
        const char* familyId = FAMILY_CHAT_ID[data->id];
        if (familyId && familyId[0]) {
            if (sendTelegramTo(familyId, msg)) {
                anySuccess = true;
            }
        }
    }

    // Cap nhat cooldown neu it nhat 1 ben nhan duoc
    if (anySuccess) {
        data->lastAlertSent = now;
    }
}
