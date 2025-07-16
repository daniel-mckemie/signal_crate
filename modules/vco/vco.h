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

typedef struct {
    float frequency;
    float amplitude;
    Waveform waveform;
	float phase;
    float sample_rate;
	float tri_state;
	float current_freq_display;
    CParamSmooth smooth_freq;
    CParamSmooth smooth_amp;
    pthread_mutex_t lock;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} VCO;

#endif
