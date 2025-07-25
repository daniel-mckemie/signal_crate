#ifndef VCO_H
#define VCO_H

#include <pthread.h>
#include "util.h"
#include "module.h"

typedef enum {
    WAVE_SINE,
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_TRIANGLE
} Waveform;

typedef enum {
	RANGE_LOW,
	RANGE_MID,
	RANGE_FULL,
	RANGE_SUPER
} RangeMode;

typedef struct {
    float frequency;
    float amplitude;
    Waveform waveform;
	RangeMode range_mode;
	float phase;
    float sample_rate;
	float tri_state;

	// For modulation UI display
	float display_freq;
	float display_amp;
    CParamSmooth smooth_freq;
    CParamSmooth smooth_amp;
    pthread_mutex_t lock;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} VCO;

#endif
