#ifndef VCA_H
#define VCA_H

#include <pthread.h>
#include <stdbool.h>

#include "util.h"

typedef struct {
    int target_channel;

    float gain;
    float pan;

    float display_gain;
    float display_pan;

    CParamSmooth smooth_gain;
    CParamSmooth smooth_pan;

    bool entering_command;
    char command_buffer[64];
    int command_index;

    pthread_mutex_t lock;
} VCAState;

#endif
