#ifndef FREEVERB_H
#define FREEVERB_H

#include <pthread.h>
#include "module.h"
#include "util.h"

#define NUM_COMBS 8
#define NUM_ALLPASS 4
#define MAX_DELAY 2048

typedef struct {
    float buffer[MAX_DELAY];
    int size;
    int index;
} DelayLine;

typedef struct {
    float sample_rate;

    float feedback;    // 0.0 to <1.0
    float damping;     // 0.0 to 1.0
    float wet;         // 0.0 to 1.0

    DelayLine combs[NUM_COMBS];
    float comb_filterstore[NUM_COMBS];

    DelayLine allpasses[NUM_ALLPASS];

    float last_damp[NUM_COMBS];

    CParamSmooth smooth_feedback;
    CParamSmooth smooth_damping;
    CParamSmooth smooth_wet;

    pthread_mutex_t lock;

    float display_feedback;
    float display_damping;
    float display_wet;

    bool entering_command;
    char command_buffer[64];
    int command_index;
} Freeverb;

#endif

