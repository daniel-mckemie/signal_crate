#ifndef MOOG_FILTER_H 
#define MOOG_FILTER_H 

#include <pthread.h>
#include <portaudio.h>
#include "util.h"

#define AMPLITUDE 0.99f

typedef struct {
	float cutoff;
	float resonance;
	float sample_rate;
	float z[4]; // filter stages
	
	CParamSmooth smooth_cutoff;
	CParamSmooth smooth_resonance;

	pthread_mutex_t lock;
	volatile int running;
} MoogFilter;

int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userdata);

void clamp_params(MoogFilter *state);

#endif

