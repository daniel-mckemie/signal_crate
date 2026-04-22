#ifndef INPUT_H
#define INPUT_H

#include "util.h"
#include <pthread.h>

typedef struct {
    float gain;
    float sample_rate;

    int channel_index;

    CParamSmooth smooth_gain;

    pthread_mutex_t lock;

    // For command mode
    bool entering_command;
    char command_buffer[64];
    int command_index;
} InputState;

#endif
