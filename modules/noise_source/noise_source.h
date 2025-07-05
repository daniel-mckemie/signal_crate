#ifndef NOISE_SOURCE_H
#define NOISE_SOURCE_H

#include <pthread.h>
#include "module.h"
#include "util.h"
#include "pink_filter.h"

typedef enum {
	WHITE_NOISE,
	PINK_NOISE,
	BROWN_NOISE
} NoiseType;

typedef struct {
	float amplitude;
	NoiseType noise_type;
	float sample_rate;

	CParamSmooth smooth_amp;

	PinkFilter pink;

	pthread_mutex_t lock;

	bool entering_command;
	char command_buffer[64];
	int command_index;
} NoiseSource;

#endif
