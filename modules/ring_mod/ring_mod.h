#ifndef RING_MOD_H
#define RING_MOD_H

#include <pthread.h>
#include "module.h"
#include "util.h"

typedef struct {
    float sample_rate;
	float phase;
	float mod_freq;
	float car_amp;
	float mod_amp;

	CParamSmooth smooth_mod_freq;
	CParamSmooth smooth_car_amp;
	CParamSmooth smooth_mod_amp;

    pthread_mutex_t lock;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} RingMod;

Module* create_module(float sample_rate);

#endif
