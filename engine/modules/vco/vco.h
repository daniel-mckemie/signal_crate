#ifndef VCO_H
#define VCO_H

#include <pthread.h>
#include "util.h"

typedef enum {
    WAVE_SINE,
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_TRIANGLE
} Waveform;

typedef struct {
    float frequency;
    float amplitude;
    Waveform waveform;
    float sample_rate;
    CParamSmooth smooth_freq;
    CParamSmooth smooth_amp;
    pthread_mutex_t lock;
} VCO;

#endif
