#ifndef AMP_MOD_H
#define AMP_MOD_H

#include <pthread.h>
#include "module.h"
#include "util.h"

typedef struct {
	float sample_rate;
	float phase;
	float freq;
	float car_amp;
	float depth;

	CParamSmooth smooth_freq;
	CParamSmooth smooth_car_amp;
	CParamSmooth smooth_depth;

	pthread_mutex_t lock;

	float display_freq;
	float display_car_amp;
	float display_depth;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} AmpMod;

Module* create_module(float sample_rate);

#endif
