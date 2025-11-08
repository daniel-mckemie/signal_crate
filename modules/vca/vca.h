#ifndef OUTPUT_H
#define OUTPUT_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    float gain;
	float pan;
	float display_gain;
	float display_pan;

	CParamSmooth smooth_gain;
	CParamSmooth smooth_pan;

    // Command mode
    bool entering_command;
    char command_buffer[64];
    int command_index;

    pthread_mutex_t lock;
} VCAState;

#endif

