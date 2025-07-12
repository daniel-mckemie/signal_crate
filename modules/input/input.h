#ifndef INPUT_H
#define INPUT_H

#include <pthread.h>
#include "util.h"

typedef struct {
	float amp;
	float sample_rate;

	CParamSmooth smooth_amp;
	
	pthread_mutex_t lock;

	// For command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;
} InputState;

#endif
