#ifndef PLAYER_H
#define PLAYER_H

#include <pthread.h>
#include <stdbool.h>
#include "module.h"
#include "util.h"

typedef struct {
	float sample_rate;
	float file_rate;
	float* data;
	unsigned long num_frames;

	double play_pos;
	double external_play_pos;
	double scrub_target;
	float playback_speed;
	float amp;

	CParamSmooth smooth_speed;
	CParamSmooth smooth_amp;

	pthread_mutex_t lock;

	// Display
	double display_pos;
	float display_speed;
	float display_amp;
	bool playing;

	// Command input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} Player;

#endif

