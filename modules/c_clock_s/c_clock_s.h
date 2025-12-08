#ifndef C_CLOCK_S_H
#define C_CLOCK_S_H

#include <pthread.h>
#include <stdbool.h>
#include "util.h"

/*
 * CClockS represents a single clock module that is syncable to others like it
 *
 * All clocks share BPM and run state via a global "primary" which is driven
 * by the primary clock (the one with no control inputs).  Secondary clocks
 * (those patched with `in=clk1` etc.) follow the primary's BPM/run, but keep
 * their own mult and pulse-width, and can be locally enabled/disabled via
 * user_enable.
 */
typedef struct {
    float bpm;
    float pw;
    float mult;

    float last_gate;
    double phase;
    float sample_rate;
    int running;

    int user_enable;

	int pending_resync;
	float last_sync_in;

    float display_bpm;
    float display_pw;
    float display_mult;
    int   display_running;

    bool  entering_command;
    char  command_buffer[64];
    int   command_index;

    pthread_mutex_t lock;
} CClockS;

#endif

