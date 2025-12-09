#ifndef C_SH_H
#define C_SH_H

#include <stdbool.h>
#include <pthread.h>
#include "util.h"

typedef struct {
    // base params
    float rate_hz;      // internal sample rate in Hz (used if no trig input)
    float depth;        // scales the sampled value
    float sample_rate;

    // state
    float phase;        // for internal clock
    float current_val;  // held output value
    float display_val;  // UI display value

    float last_trig;    // last trigger sample for edge detect

    // smoothing for params
    CParamSmooth smooth_rate;
    CParamSmooth smooth_depth;

    // UI / command
    pthread_mutex_t lock;
    bool  entering_command;
    char  command_buffer[64];
    int   command_index;

} CSH;

#endif

