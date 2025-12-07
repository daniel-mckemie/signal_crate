#ifndef C_CLOCK_H
#define C_CLOCK_H

#include <pthread.h>
#include <stdbool.h>
#include <util.h>

typedef struct {
	float bpm;
	float pw; // pulse width
	float mult; // Mult and Div

	float last_gate;
	float phase;
	float sample_rate;

	int running;

	float display_bpm;
	float display_pw;
	float display_mult;

	CParamSmooth smooth_bpm;
	CParamSmooth smooth_pw;
	CParamSmooth smooth_mult;

	// Command mode
	bool entering_command;
	char command_buffer[64];
	int command_index;

	pthread_mutex_t lock;
} CClockState;

#endif

