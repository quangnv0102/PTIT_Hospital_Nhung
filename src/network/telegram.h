#pragma once
#include "../config.h"

// Gui tin den chat_id cu the. Neu chatId rong hoac NULL -> skip.
// Tra ve true neu HTTP 200.
bool sendTelegramTo(const char* chatId, const String& message);

// Backward compat: gui den CHAT_ID_DOCTOR (bac si).
// Dung cho cac tin he thong (mat cam bien, boot,...).
bool sendTelegram(const String& message);

// Gui canh bao BN theo phan quyen:
//   - Bac si: luon nhan
//   - Nguoi nha cua BN: chi nhan neu la BN cua ho
// Co cooldown TELEGRAM_COOLDOWN_MS, cap nhat data->lastAlertSent.
void sendAlert(PatientData* data);
