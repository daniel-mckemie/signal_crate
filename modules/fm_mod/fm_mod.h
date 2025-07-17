#ifndef FM_MOD_H
#define FM_MOD_H

#include <pthread.h>
#include "util.h"

typedef struct {
	float modulator_phase;
	float mod_freq;
	float index;
	float sample_rate;
	CParamSmooth smooth_freq;
	CParamSmooth smooth_index;
	pthread_mutex_t lock;

	float display_freq;
	float display_index;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} FMMod;

#endif
	

