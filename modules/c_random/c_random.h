#ifndef C_RANDOM_H
#define C_RANDOM_H

#include <pthread.h>
#include <stdbool.h>
#include "util.h"
#include "pink_filter.h"
#include "brown_noise.h"

typedef enum {
    RAND_WHITE = 0,
    RAND_PINK  = 1,
    RAND_BROWN = 2
} RandomType;

typedef struct {
    float rate_hz;
    float depth;          // <-- REQUIRED
    float range_min;
    float range_max;

    float sample_rate;
    float phase;

    float current_val;
    float display_val;

    RandomType type;

    PinkFilter pink;
    BrownNoise brown;

    CParamSmooth smooth_rate;
    CParamSmooth smooth_depth;   // <-- REQUIRED

    pthread_mutex_t lock;

    bool entering_command;
    char command_buffer[64];
    int command_index;

} CRandom;

#endif

