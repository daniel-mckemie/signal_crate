#ifndef LOOPER_H
#define LOOPER_H

#include <pthread.h>
#include "module.h"
#include "util.h"

#define LOOP_FADE_SAMPLES 128

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
	unsigned long max_buffer_len;

	double read_pos;
	double write_pos;
	unsigned long loop_start;
	unsigned long loop_end;
	float playback_speed;
	float amp;
	LooperState looper_state;

	CParamSmooth smooth_speed;
	CParamSmooth smooth_amp;

	pthread_mutex_t lock;

	float display_playback_speed;
	float display_amp;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} Looper;

#endif

