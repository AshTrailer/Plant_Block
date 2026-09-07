#include "data_processor.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

float data_processor_mean(const int *data, int count) {
    if (count <= 0) return 0;
    long long sum = 0;
    for (int i = 0; i < count; i++) {
        sum += data[i];
    }
    return (float)sum / count;
}

float data_processor_stddev(const int *data, int count) {
    if (count <= 1) return 0;
    float mean = data_processor_mean(data, count);
    double sum_sq = 0;
    for (int i = 0; i < count; i++) {
        float diff = data[i] - mean;
        sum_sq += diff * diff;
    }
    return (float)sqrt(sum_sq / (count - 1));
}

int data_processor_remove_outliers(const int *input, int count, int *output, int min_samples) {
    if (count < min_samples) return 0;
    if (count < 3) {
        memcpy(output, input, count * sizeof(int));
        return count;
    }
    int max_val = input[0], min_val = input[0];
    int max_idx = 0, min_idx = 0;
    for (int i = 1; i < count; i++) {
        if (input[i] > max_val) { max_val = input[i]; max_idx = i; }
        if (input[i] < min_val) { min_val = input[i]; min_idx = i; }
    }
    int out_idx = 0;
    for (int i = 0; i < count; i++) {
        if (i == max_idx || i == min_idx) continue;
        output[out_idx++] = input[i];
    }
    if (out_idx < min_samples) return 0;
    return out_idx;
}

bool data_processor_check_stability(const int *data, int count, float max_stddev_percent, int max_retry, int *final_data, int *final_count) {
    (void)max_retry; // 此函数仅判断一次，重试需由调用者完成
    int *temp = (int*)malloc(count * sizeof(int));
    if (!temp) return false;
    int new_count = data_processor_remove_outliers(data, count, temp, 3);
    if (new_count < 3) {
        free(temp);
        return false;
    }
    float stddev = data_processor_stddev(temp, new_count);
    float mean = data_processor_mean(temp, new_count);
    if (mean == 0) {
        free(temp);
        return false;
    }
    float percent = (stddev / mean) * 100.0f;
    if (percent <= max_stddev_percent) {
        memcpy(final_data, temp, new_count * sizeof(int));
        *final_count = new_count;
        free(temp);
        return true;
    }
    free(temp);
    return false;
}

void data_processor_linear_fit(float x1, float y1, float x2, float y2, float *k, float *b) {
    if (x2 == x1) {
        *k = 0;
        *b = y1;
    } else {
        *k = (y2 - y1) / (x2 - x1);
        *b = y1 - (*k) * x1;
    }
}

float data_processor_apply_linear(int raw, float k, float b) {
    return k * raw + b;
}