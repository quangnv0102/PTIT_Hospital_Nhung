#include "alert.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool checkThresholds(PatientData* data) {
    data->prevState = data->state;

    if (!data->sensorConnected) {
        // Cảm biến mất kết nối — không thay đổi state alert, xử lý riêng ở main
        return false;
    }

    if (data->fallDetected) {
        data->state = STATE_FALL;
    } else if (data->spo2 < SPO2_CRITICAL ||
               data->hr  < HR_CRITICAL_LOW ||
               data->hr  > HR_CRITICAL_HIGH) {
        data->state = STATE_CRITICAL;
    } else if (data->spo2 < SPO2_WARNING ||
               data->hr  < HR_WARNING_LOW ||
               data->hr  > HR_WARNING_HIGH) {
        data->state = STATE_WARNING;
    } else {
        data->state = STATE_NORMAL;
    }

    return (data->state != data->prevState);
}

String buildAlertMessage(const PatientData* data) {
    // Lấy thời gian hệ thống dạng HH:MM:SS từ millis()
    unsigned long t = millis() / 1000UL;
    char timeStr[12];
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu",
             t / 3600UL, (t % 3600UL) / 60UL, t % 60UL);

    String patName = "Benh nhan #" + String(data->id + 1);
    String msg;

    switch (data->state) {
        case STATE_WARNING:
            msg  = "!! CANH BAO -- " + patName + "\n";
            msg += "SpO2 : " + String((int)data->spo2) + "%  (nguong: <94%)\n";
            msg += "HR   : " + String((int)data->hr)   + " BPM\n";
            msg += "[" + String(timeStr) + "]";
            break;

        case STATE_CRITICAL:
            msg  = "!!! NGUY HIEM -- " + patName + "\n";
            msg += "SpO2 : " + String((int)data->spo2) + "%  (nguong: <90%)\n";
            msg += "HR   : " + String((int)data->hr)   + " BPM\n";
            msg += "[" + String(timeStr) + "]";
            break;

        case STATE_FALL:
            msg  = "PHAT HIEN NGA -- " + patName + "\n";
            msg += "SpO2 : " + String((int)data->spo2) + "%\n";
            msg += "HR   : " + String((int)data->hr)   + " BPM\n";
            msg += "Can kiem tra ngay!\n";
            msg += "[" + String(timeStr) + "]";
            break;

        case STATE_NORMAL:
        default:
            msg  = "BINH THUONG -- " + patName + "\n";
            msg += "SpO2 : " + String((int)data->spo2) + "%\n";
            msg += "HR   : " + String((int)data->hr)   + " BPM\n";
            msg += "[" + String(timeStr) + "]";
            break;
    }
    return msg;
}

void triggerBuzzer(PatientState state) {
    switch (state) {
        case STATE_WARNING:
            // 3 tiếng dài — nghe rõ hơn
            for (int i = 0; i < 10; i++) {
                tone(PIN_BUZZER, 2000, 500);  // 2kHz, 500ms
                vTaskDelay(pdMS_TO_TICKS(700));
            }
            break;

        case STATE_CRITICAL:
            // 5 tiếng gấp, tần số cao hơn
            for (int i = 0; i < 15; i++) {
                tone(PIN_BUZZER, 3000, 400);  // 3kHz, 400ms
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            break;

        case STATE_FALL:
            // Liên tục 8 tiếng rất gấp
            for (int i = 0; i < 20; i++) {
                tone(PIN_BUZZER, 4000, 300);  // 4kHz, 300ms
                vTaskDelay(pdMS_TO_TICKS(350));
            }
            break;

        case STATE_NORMAL:
        default:
            noTone(PIN_BUZZER);
            break;
    }
}
