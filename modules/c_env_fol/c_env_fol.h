#ifndef C_LFO_H
#define C_LFO_H

#include <pthread.h>
#include "util.h"
#include "module.h"

typedef struct {
    float attack_ms;
    float decay_ms;
	float sensitivity;
    float env;
	float threshold;
	float smoothed_env;
    float sample_rate;

	float display_env;

    CParamSmooth smooth_attack;
    CParamSmooth smooth_decay;
	CParamSmooth smooth_sens;

    pthread_mutex_t lock;

	float display_freq;
	float display_amp;

    // Command mode
    bool entering_command;
    char command_buffer[64];
    int command_index;
} CEnvFol;

#endif

