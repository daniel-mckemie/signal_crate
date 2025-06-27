#ifndef AUDIO_H
#define AUDIO_H

#include <pthread.h>
#include <portaudio.h>
#include "util.h"

typedef enum {
	WAVE_SINE,
	WAVE_SAW,
	WAVE_SQUARE,
	WAVE_TRIANGLE
} Waveform;
// This is the core of the module, defining params
typedef struct {
	float frequency; 
	float amplitude; 
	Waveform waveform;
	float sample_rate;

	CParamSmooth smooth_freq;
	CParamSmooth smooth_amp;

	pthread_mutex_t lock; // Used for threading when creating a UI
	volatile int running; // Used to cleanly quit
} VCO;

// define callback, should stay as-is
int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData);

void clamp_params(VCO *state); // To use where needed

#endif
