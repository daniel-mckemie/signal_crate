#ifndef SPEC_HOLD
#define SPEC_HOLD

#include <pthread.h>
#include <fftw3.h>
#include "module.h"
#include "util.h"

typedef struct {
	float sample_rate;
	float tilt; // -1.0 to +1.0 (negative = dark, positive = light)
	float pivot_hz; // point of tilt

	bool freeze;
	float* frozen_mag;
	float* frozen_phase;

	CParamSmooth smooth_tilt;
	CParamSmooth smooth_pivot_hz;

	pthread_mutex_t lock;

	float display_tilt;
	float display_pivot;

	// FFT state
	float* time_buffer;
	fftwf_complex* freq_buffer;
	fftwf_plan fft_plan;
	fftwf_plan ifft_plan;

	// Overlap-add buffers
	float* input_buffer;
	float* output_buffer;

	int hop_write_index;
	unsigned int in_write_index;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} SpecHold;

#endif
