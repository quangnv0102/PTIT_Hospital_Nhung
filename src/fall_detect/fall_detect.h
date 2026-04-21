#pragma once
#include "../config.h"

// Khởi tạo toàn bộ state machine cho các bệnh nhân
void fallDetectInit();

// Chạy FSM phát hiện ngã cho bệnh nhân patientId với gia tốc (ax, ay, az) tính bằng g.
// Trả về true khi xác nhận ngã, false trong các trường hợp còn lại.
bool detectFall(int patientId, float ax, float ay, float az);

// Reset state machine về IDLE sau khi đã xử lý cảnh báo ngã
void resetFallState(int patientId);
