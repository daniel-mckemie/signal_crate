#ifndef TEMPLATE_H
#define TEMPLATE_H

#include <pthread.h>
#include "util.h"

typedef enum {
	CHOICE_1,
	CHOICE_2,
	CHOICE_3
} CustomType;

typedef struct {
	float param1;
	float param2;
	
	CustomType cust_type;
	
	// put these for all
	float sample_rate;

	// UI display params, any that you want to have in UI
	float display_param1;
	float display_param2;

	// Have these for UI
	CParamSmooth smooth_param1;
	CParamSmooth smooth_param2;
	pthread_mutex_t lock;

	// For command mode, keyboard control
	bool entering_command;
	char command_buffer[64];
	int command_index;
} Template; // Name of module

#endif
