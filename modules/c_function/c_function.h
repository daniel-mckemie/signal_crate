#ifndef C_FUNCTION_H
#define C_FUNCTION_H

#include <pthread.h>
#include "util.h"
#include "module.h"

typedef enum { ENV_IDLE, ENV_ATTACK, ENV_SUSTAIN, ENV_RELEASE } EnvState;

typedef struct {
    float attack_time;
    float release_time;
	float depth;
    float envelope_out;

    float timer;

    float sample_rate;
    EnvState state;

    bool cycle;
	bool cycle_stop_requested;
	bool trigger_held;
    bool short_mode;
	bool gate_prev;
	bool trig_prev;
	bool cycle_prev_cv;

	float attack_start_level;
	float release_start_level;
    float threshold_trigger;
    float threshold_cycle;
	float threshold_gate;

    // Smoothed control inputs
    CParamSmooth smooth_att;
    CParamSmooth smooth_rel;
	CParamSmooth smooth_depth;

    pthread_mutex_t lock;

	float display_att;
	float display_rel;
	float display_cycle;
	float display_depth;
    
	// UI and command state
    bool entering_command;
    char command_buffer[64];
    int command_index;
} CFunction;

#endif
