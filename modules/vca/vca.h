#ifndef OUTPUT_H
#define OUTPUT_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    _Atomic float gain;              // overall gain
	_Atomic float display_gain;

	CParamSmooth smooth_gain;

    // Command mode
    bool entering_command;
    char command_buffer[64];
    int command_index;

    pthread_mutex_t lock;
} VCAState;

#endif

