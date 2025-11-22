#ifndef C_INPUT_H
#define C_INPUT_H

#include <pthread.h>
#include "util.h"

typedef struct {
    int channel_index;    // which input channel the ENGINE extracts

    float sample_rate;
    float last_val;

    CParamSmooth smooth;

    pthread_mutex_t lock;

    // UI command mode
    bool entering_command;
    char command_buffer[64];
    int command_index;

} CInputState;

#endif

