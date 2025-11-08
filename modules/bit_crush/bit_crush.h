#ifndef BIT_CRUSH_H
#define BIT_CRUSH_H

#include <pthread.h>
#include <stdbool.h>
#include "util.h"

typedef struct {
    float bits;               // 1–16 bit depth
    float rate;               // Hz (sampling frequency)
    float display_bits;
    float display_rate;

    float last_sample;
    float phase;
	float sample_rate;

    CParamSmooth smooth_bits;
    CParamSmooth smooth_rate;

    bool entering_command;
    char command_buffer[64];
    int command_index;

    pthread_mutex_t lock;
} BitCrushState;

#endif

