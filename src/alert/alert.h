#pragma once
#include "../config.h"

// So sánh HR, SpO2, fallDetected với ngưỡng trong config.h.
// Cập nhật data->state và data->prevState.
// Trả về true nếu state thay đổi (cần gửi cảnh báo mới).
bool checkThresholds(PatientData* data);

// Tạo nội dung tin nhắn Telegram theo trạng thái hiện tại của bệnh nhân.
String buildAlertMessage(const PatientData* data);

// Điều khiển Buzzer theo mức độ cảnh báo:
//   WARNING  → 1 tiếng
//   CRITICAL → 3 tiếng
//   FALL     → liên tục (5 tiếng ngắn)
//   NORMAL   → tắt
void triggerBuzzer(PatientState state);
