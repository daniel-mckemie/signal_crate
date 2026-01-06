#ifndef C_SH_H
#define C_SH_H

#include <stdbool.h>
#include <pthread.h>
#include "util.h"

typedef struct {
    float rate_hz;
    float depth;
    float sample_rate;

    float phase;
    float current_val;
    float display_val;
	float display_rate;
	float display_depth;
    float last_trig;

    CParamSmooth smooth_rate;
    CParamSmooth smooth_depth;

    pthread_mutex_t lock;
    bool  entering_command;
    char  command_buffer[64];
    int   command_index;

} CSH;

#endif

