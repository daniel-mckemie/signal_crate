#ifndef RING_MOD_H
#define RING_MOD_H

#include <pthread.h>
#include "module.h"
#include "util.h"

typedef struct {
    float sample_rate;
	float car_amp;
	float mod_amp;
	float depth;

	CParamSmooth smooth_car_amp;
	CParamSmooth smooth_mod_amp;
	CParamSmooth smooth_depth;

    pthread_mutex_t lock;

	float display_car_amp;
	float display_mod_amp;
	float display_depth;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} RingMod;

#endif
