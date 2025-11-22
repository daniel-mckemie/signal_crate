#ifndef C_OUTPUT_H
#define C_OUTPUT_H

#include <pthread.h>
#include <stdbool.h>
#include <util.h>

// Control Output Module
// Emits a DC (control-rate) signal usable by external circuitry

typedef struct {
	float value;
	float display_value;
	CParamSmooth smooth_val;
	int target_channel;

	// Command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;

	pthread_mutex_t lock;
} COutputState;

#endif
