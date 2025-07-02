#ifndef RING_MOD_H
#define RING_MOD_H

#include <pthread.h>
#include "module.h"
#include "util.h"

typedef struct {
    float sample_rate;
	float phase;
	float freq;
	float amp1;
	float amp2;

	CParamSmooth smooth_freq;
	CParamSmooth smooth_amp1;
	CParamSmooth smooth_amp2;

    pthread_mutex_t lock;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} RingMod;

Module* create_module(float sample_rate);

#endif

