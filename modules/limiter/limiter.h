#ifndef LIMITER_H
#define LIMITER_H

#include "util.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    float threshold;
    float release;
    float lookahead_ms;
    float sample_rate;

    // Internal state for limiting
    float envelope;
    float *delay_buffer;
    int delay_samples;
    int delay_index;

    // UI display params
    float display_threshold;
    float display_release;
    float display_reduction;

    // Parameter smoothing
    CParamSmooth smooth_threshold;
    CParamSmooth smooth_release;

    pthread_mutex_t lock;

    // For command mode, keyboard control
    bool entering_command;
    char command_buffer[64];
    int command_index;
} LimiterState;

#endif
