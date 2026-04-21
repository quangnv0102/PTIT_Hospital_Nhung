#include "filter.h"
#include <math.h>

float movingAverage(FilterBuffer* buf, float* buffer, float newValue) {
    buffer[buf->index] = newValue;

    int count = buf->filled ? FILTER_WINDOW : (buf->index + 1);
    float sum = 0.0f;
    for (int i = 0; i < count; i++) {
        sum += buffer[i];
    }
    return sum / (float)count;
}

void advanceFilter(FilterBuffer* buf) {
    buf->index = (buf->index + 1) % FILTER_WINDOW;
    if (buf->index == 0) {
        buf->filled = true;
    }
}

float calcSMA(float ax, float ay, float az) {
    return sqrtf(ax * ax + ay * ay + az * az);
}
