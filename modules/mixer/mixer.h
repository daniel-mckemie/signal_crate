#ifndef MIXER_H
#define MIXER_H

#include "util.h"
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    float sample_rate;

    float gain;
    CParamSmooth smooth_gain;

    float display_gain;

    bool entering_command;
    char command_buffer[64];
    int command_index;

    pthread_mutex_t lock;
} MixerState;

#endif
