#include "fall_detect.h"
#include "../utils/filter.h"

// Trạng thái nội bộ của FSM phát hiện ngã
enum FallFSMState {
    FALL_FSM_IDLE,    // chờ va đập
    FALL_FSM_IMPACT,  // đã ghi nhận va đập, chờ bất động để xác nhận
    FALL_FSM_CONFIRM  // bất động xác nhận — sẽ trả về true ở chu kỳ tiếp theo
};

struct FallDetector {
    FallFSMState  state;
    unsigned long impactTime;
};

static FallDetector detectors[MAX_PATIENTS];

void fallDetectInit() {
    for (int i = 0; i < MAX_PATIENTS; i++) {
        detectors[i].state      = FALL_FSM_IDLE;
        detectors[i].impactTime = 0;
    }
}

// Thuật toán 2 ngưỡng theo tài liệu:
//   1. Tính SMA = sqrt(ax^2 + ay^2 + az^2)
//   2. SMA > FALL_IMPACT_G → IMPACT, ghi timestamp
//   3. Trong FALL_CONFIRM_MS: SMA < FALL_STILL_G → CONFIRM → trả về true chu kỳ sau
//   4. Hết FALL_CONFIRM_MS mà không bất động → false alarm, reset IDLE
bool detectFall(int patientId, float ax, float ay, float az) {
    float sma = calcSMA(ax, ay, az);
    FallDetector* d = &detectors[patientId];

    switch (d->state) {
        case FALL_FSM_IDLE:
            if (sma > FALL_IMPACT_G) {
                d->state      = FALL_FSM_IMPACT;
                d->impactTime = millis();
            }
            break;

        case FALL_FSM_IMPACT:
            if ((millis() - d->impactTime) > FALL_CONFIRM_MS) {
                // Hết cửa sổ, không có bất động → false alarm
                d->state = FALL_FSM_IDLE;
            } else if (sma < FALL_STILL_G) {
                // Bất động trong cửa sổ → chuyển sang CONFIRM
                d->state = FALL_FSM_CONFIRM;
            }
            break;

        case FALL_FSM_CONFIRM:
            // Trả về true và reset để sẵn sàng phát hiện lần tiếp
            d->state = FALL_FSM_IDLE;
            return true;

        default:
            d->state = FALL_FSM_IDLE;
            break;
    }
    return false;
}

void resetFallState(int patientId) {
    detectors[patientId].state      = FALL_FSM_IDLE;
    detectors[patientId].impactTime = 0;
}
