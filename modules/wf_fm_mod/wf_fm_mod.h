#ifndef WF_FM_MOD_H
#define WF_FM_MOD_H

#include <pthread.h>
#include "util.h"

typedef struct {
	float modulator_phase;
	float modulator_freq;
	float index;
	float fold_threshold_mod;
	float fold_threshold_car;
	float blend;
	float sample_rate;
	CParamSmooth smooth_freq;
	CParamSmooth smooth_index;
	CParamSmooth smooth_blend;
	CParamSmooth smooth_fold_mod;
	CParamSmooth smooth_fold_car;
	pthread_mutex_t lock;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} WFFMMod;

#endif
	
