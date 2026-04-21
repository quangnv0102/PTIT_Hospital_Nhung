#pragma once
#include "../config.h"

// Cấu hình PubSubClient và kết nối đến MQTT broker.
// Gọi trong setup() của main sau khi WiFi đã kết nối.
void mqttSetup();

// Duy trì kết nối MQTT: tự động reconnect nếu mất, gọi client.loop().
// Gọi định kỳ từ taskMQTT.
void mqttLoop();

// Publish dữ liệu 1 bệnh nhân lên 4 topic MQTT:
//   hospital/ward1/patient/{id}/hr
//   hospital/ward1/patient/{id}/spo2
//   hospital/ward1/patient/{id}/state
//   hospital/ward1/patient/{id}/fall
void publishPatient(const PatientData* data);
