#pragma once
#include "../config.h"

// Ghi newValue vào buffer tại vị trí index hiện tại, trả về giá trị trung bình.
// Gọi advanceFilter() một lần sau khi đã gọi xong cho cả HR và SpO2.
float movingAverage(FilterBuffer* buf, float* buffer, float newValue);

// Tiến index lên 1; đánh dấu filled=true khi buffer đã đầy lần đầu.
void advanceFilter(FilterBuffer* buf);

// Tính Signal Magnitude Area: sqrt(ax^2 + ay^2 + az^2)
float calcSMA(float ax, float ay, float az);
