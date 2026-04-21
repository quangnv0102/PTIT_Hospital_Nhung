#pragma once
#include "../config.h"
#include <Wire.h>

// Khởi tạo Wire Bus 0, TCA9548A và các cảm biến.
// Gọi trong setup() của main trước khi tạo task.
void sensorSetup();

// Chọn kênh TCA9548A trước mỗi lần đọc cảm biến.
// LUÔN gọi hàm này trước khi đọc bất kỳ cảm biến nào.
void selectChannel(uint8_t channel);

// Kiểm tra một địa chỉ I2C có phản hồi không (phát hiện sensor detach).
bool isDeviceConnected(uint8_t addr);

// Đọc nhịp tim và SpO2 cho bệnh nhân patientId.
// Khi USE_MOCK_DATA=1: trả về số giả lập biến động tự nhiên.
// Khi USE_MOCK_DATA=0: đọc thật từ SparkFun MAX3010x.
void readMAX30102(int patientId, float* hr, float* spo2);

// Đọc gia tốc 3 trục cho bệnh nhân patientId.
// Khi USE_MOCK_DATA=1: trả về giá trị gần 1g trục Z.
// Khi USE_MOCK_DATA=0: đọc thật từ Adafruit MPU6050.
void readMPU6050(int patientId, float* ax, float* ay, float* az);

// Gọi readMAX30102 + readMPU6050, cập nhật toàn bộ PatientData (raw).
// Cập nhật sensorConnected, lastUpdate.
void readPatient(int patientId, PatientData* data);
