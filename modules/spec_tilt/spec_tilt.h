#ifndef SPEC_TILT
#define SPEC_TILT

#include <pthread.h>
#include <fftw3.h>
#include "module.h"
#include "util.h"

typedef struct {
	float sample_rate;
	float tilt; // -1.0 to +1.0 (negative = dark, positive = light)

	CParamSmooth smooth_tilt;

	pthread_mutex_t lock;

	// FFT state
	float* time_buffer;
	fftwf_complex* freq_buffer;
	fftwf_plan fft_plan;
	fftwf_plan ifft_plan;

	// Overlap-add buffers
	float* input_buffer;
	float* output_buffer;

	int hop_write_index;

	// For command mode input
	bool entering_command;
	char command_buffer[64];
	int command_index;
} SpecTilt;

Module* create_module(float sample_rate);

#endif
