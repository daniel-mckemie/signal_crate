#ifndef FM_MOD_H
#define FM_MOD_H

#include <pthread.h>
#include "util.h"

#define FM_MOD_MIN_FREQ 0.01f
#define FM_MOD_MAX_FREQ 21600.0f
#define FM_MOD_MAX_INDEX 10.0f
#define HILBERT_LEN 129

typedef struct {
	float hilbert_delay[HILBERT_LEN];
	int hilbert_pos;

	float carrier_phase;
	float modulator_phase;
	float mod_freq;
	float car_amp;
	float mod_amp;
	float index;
	float sample_rate;
	CParamSmooth smooth_freq;
	CParamSmooth smooth_car_amp;
	CParamSmooth smooth_mod_amp;
	CParamSmooth smooth_index;
	pthread_mutex_t lock;

	float display_freq;
	float display_car_amp;
	float display_mod_amp;
	float display_index;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} FMMod;

#endif
	

