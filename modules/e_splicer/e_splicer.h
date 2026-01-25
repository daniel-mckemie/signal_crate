#ifndef E_SPLICER_H
#define E_SPLICER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "module.h"

typedef struct {
    pthread_mutex_t lock;

    double* data;
    uint64_t frames;
    int channels;
    int file_sr;
    int format;
	int valid;
	char error[128];

    uint64_t playhead;
    bool playing;
	float playback_speed;
	float speed_accum;

    bool cut_a_set;
    bool cut_b_set;
    uint64_t cut_a;
    uint64_t cut_b;

    bool entering_command;
    char command_buffer[64];
    int command_index;

    char src_path[512];
    char stem[256];

    char status[128];

} ESplicer;

Module* create_module(const char* args, float sample_rate);

#endif

