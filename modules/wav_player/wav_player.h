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

	float play_pos;
	float external_play_pos;
	float scrub_target;
	float playback_speed;

	CParamSmooth smooth_speed;

	pthread_mutex_t lock;

	// Display
	float display_pos;
	float display_speed;
	bool playing;

	// Command input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} Player;

Module* create_module(const char* args, float sample_rate);

#endif

