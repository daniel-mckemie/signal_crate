#ifndef C_LFO_H
#define C_LFO_H

#include <pthread.h>
#include "util.h"
#include "module.h"

typedef enum {
    LFO_SINE,
    LFO_SAW,
    LFO_SQUARE,
    LFO_TRIANGLE
} LFOWaveform;

typedef struct {
    float frequency;
    float amplitude;
    float phase;
    float tri_state;
	float depth;
	int polarity;
    float sample_rate;
    LFOWaveform waveform;

    CParamSmooth smooth_freq;
    CParamSmooth smooth_amp;
	CParamSmooth smooth_depth;

    pthread_mutex_t lock;

	float display_freq;
	float display_amp;
	float display_depth;
	LFOWaveform display_wave;

    // Command mode
    bool entering_command;
    char command_buffer[64];
    int command_index;
} CLFO;

#endif

