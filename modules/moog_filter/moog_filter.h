#ifndef MOOG_FILTER_H
#define MOOG_FILTER_H

#include <pthread.h>
#include "util.h"

typedef enum {
	LOWPASS,
	HIGHPASS,
	BANDPASS,
	NOTCH,
	RESONANT
} FilterType;

typedef struct {
	float cutoff;
	float resonance;
	float z[4]; // filter stages
	float sample_rate;

	FilterType filt_type;

	CParamSmooth smooth_co;
	CParamSmooth smooth_res;
	pthread_mutex_t lock;

	float display_cutoff;
	float display_resonance;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} MoogFilter;

#endif
