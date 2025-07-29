#ifndef C_FLUCT_H
#define C_FLUCT_H

#include <pthread.h>
#include "util.h"
#include "module.h"

typedef enum {
	FLUCT_NOISE,
	FLUCT_WALK
} FluctMode;

typedef struct {
    float rate;
    float depth;
    float phase;
    float prev_value;
    float target_value;
    float current_value;
    float sample_rate;

    FluctMode mode;

    CParamSmooth smooth_rate;
    CParamSmooth smooth_depth;

    pthread_mutex_t lock;

    float display_rate;
    float display_depth;

    // Command mode
    bool entering_command;
    char command_buffer[64];
    int command_index;
} CFluct;

#endif
