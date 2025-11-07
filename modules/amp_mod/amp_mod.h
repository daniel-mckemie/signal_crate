#ifndef AMP_MOD_H
#define AMP_MOD_H

#include <pthread.h>
#include "module.h"
#include "util.h"

typedef struct {
	float sample_rate;

	// Parameters
    float car_amp;     // amplitude of input 1
    float mod_amp;     // amplitude of input 2
    float depth;    // modulation depth

    // Smoothed versions
    CParamSmooth smooth_car_amp;
    CParamSmooth smooth_mod_amp;
    CParamSmooth smooth_depth;

    pthread_mutex_t lock;

    // For UI display
    float display_car_amp;
    float display_mod_amp;
    float display_depth;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} AmpMod;

#endif
