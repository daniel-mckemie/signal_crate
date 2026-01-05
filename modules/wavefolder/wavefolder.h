#ifndef WAVEFOLDER_H
#define WAVEFOLDER_H

#include <pthread.h>
#include "util.h"

typedef struct {
	float fold;
	float blend;
	float drive;
	float sample_rate;
	float lp_z;
	
	CParamSmooth smooth_fold;
	CParamSmooth smooth_blend;
	CParamSmooth smooth_drive;

	pthread_mutex_t lock;

	float display_fold;
	float display_blend;
	float display_drive;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} Wavefolder;

#endif
	

