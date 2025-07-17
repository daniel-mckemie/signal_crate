#ifndef LOOPER_H
#define LOOPER_H

#include <pthread.h>
#include "module.h"
#include "util.h"

typedef enum {
	IDLE,
	RECORDING,
	PLAYING,
	OVERDUBBING,
	STOPPED
} LooperState;

typedef struct {
	float sample_rate;
	float* buffer;
    unsigned long buffer_len;	

	float read_pos;
	unsigned long write_pos;
	unsigned long loop_start;
	unsigned long loop_end;
	float playback_speed;
	float current_speed_display;
	LooperState looper_state;

	CParamSmooth smooth_start;
	CParamSmooth smooth_end;
	CParamSmooth smooth_speed;

	pthread_mutex_t lock;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} Looper;

Module* create_module(float sample_rate);

#endif

