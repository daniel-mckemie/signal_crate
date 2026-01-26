#ifndef C_MIDI_TO_CV_H
#define C_MIDI_TO_CV_H

#include <pthread.h>
#include "util.h"

typedef struct {
	int chan; // 1-16, default = 0 (any channel)
    int cc; // 0..127
    float sample_rate;
    float last_val;

    CParamSmooth smooth;
    pthread_mutex_t lock;

    bool entering_command;
    char command_buffer[64];
    int command_index;
} CMidiToCVState;

#endif

