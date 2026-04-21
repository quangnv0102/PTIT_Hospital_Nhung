#include "mqtt_client.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient   wifiClient;
static PubSubClient mqttClient(wifiClient);

// Tên topic cơ sở
static const char* TOPIC_BASE = "hospital/ward1/patient";

static const char* stateStr(PatientState s) {
    switch (s) {
        case STATE_WARNING:  return "WARNING";
        case STATE_CRITICAL: return "CRITICAL";
        case STATE_FALL:     return "FALL";
        default:             return "NORMAL";
    }
}

void mqttSetup() {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setKeepAlive(30);
    Serial.printf("[MQTT] Broker: %s:%d\n", MQTT_BROKER, MQTT_PORT);
}

// Thử kết nối lại, tối đa 3 lần
static void mqttReconnect() {
    int attempts = 0;
    while (!mqttClient.connected() && attempts < 3) {
        Serial.printf("[MQTT] Ket noi... (lan %d)\n", attempts + 1);
        if (mqttClient.connect(MQTT_CLIENT_ID)) {
            Serial.println("[MQTT] Ket noi thanh cong");
        } else {
            Serial.printf("[MQTT] That bai, state=%d\n", mqttClient.state());
            delay(2000);
        }
        attempts++;
    }
}

void mqttLoop() {
    if (!mqttClient.connected()) {
        mqttReconnect();
    }
    mqttClient.loop();
}

void publishPatient(const PatientData* data) {
    if (!mqttClient.connected()) return;

    char topic[64];
    char payload[16];

    // HR
    snprintf(topic, sizeof(topic), "%s/%d/hr", TOPIC_BASE, data->id);
    snprintf(payload, sizeof(payload), "%.1f", data->hr);
    mqttClient.publish(topic, payload);

    // SpO2
    snprintf(topic, sizeof(topic), "%s/%d/spo2", TOPIC_BASE, data->id);
    snprintf(payload, sizeof(payload), "%.1f", data->spo2);
    mqttClient.publish(topic, payload);

    // State
    snprintf(topic, sizeof(topic), "%s/%d/state", TOPIC_BASE, data->id);
    mqttClient.publish(topic, stateStr(data->state));

    // Fall
    snprintf(topic, sizeof(topic), "%s/%d/fall", TOPIC_BASE, data->id);
    mqttClient.publish(topic, data->fallDetected ? "true" : "false");
}
