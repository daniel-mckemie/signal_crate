#ifndef PM_MOD_H
#define PM_MOD_H

#include <pthread.h>
#include "util.h"

typedef struct {
	float car_amp;
	float mod_amp;
	float base_freq;
	float index;

	float modulator_phase;
	float sample_rate;

	CParamSmooth smooth_car_amp;
	CParamSmooth smooth_mod_amp;
	CParamSmooth smooth_base_freq;
	CParamSmooth smooth_index;

	pthread_mutex_t lock;

	float display_car_amp;
	float display_mod_amp;
	float display_base_freq;
	float display_index;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} PMMod;

#endif
	

