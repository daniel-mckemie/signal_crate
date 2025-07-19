#ifndef DELAY_H
#define DELAY_H

#include <pthread.h>
#include "module.h"
#include "util.h"

typedef struct {
    float delay_ms;
    float mix;
    float feedback;

	float sample_rate;
    float* buffer;
    unsigned int buffer_size;
    unsigned int write_index;
    unsigned int read_index;
	float last_delay_samples;

    // Smoothing
    CParamSmooth smooth_delay;
    CParamSmooth smooth_mix;
    CParamSmooth smooth_feedback;

    pthread_mutex_t lock;

    // UI display
    float display_delay;
    float display_mix;
    float display_feedback;

    // Command input
    bool entering_command;
    char command_buffer[64];
    int command_index;
} Delay;

Module* create_module(float sample_rate);

#endif

