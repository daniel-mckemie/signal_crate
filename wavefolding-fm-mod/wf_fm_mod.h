#ifndef WF_FM_MOD_H
#define WF_FM_MOD_H
#include <pthread.h>
#include <portaudio.h>
#include "util.h"

#define AMPLITUDE 0.99f

// This is the core of the audio process with params
typedef struct {
	float modulator_phase;
	float modulator_freq;
	float index;
	float fold_threshold_mod;
	float fold_threshold_car;
	float blend;
	float sample_rate; // This is for dynamic sample rate
	pthread_mutex_t lock; // For threading with the UI
	volatile int running;

	// Smoothers
	CParamSmooth smooth_freq;
	CParamSmooth smooth_index;
	CParamSmooth smooth_blend;
	CParamSmooth smooth_fold_mod;
	CParamSmooth smooth_fold_car;
} FMState;

int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData);

void clamp_params(FMState *state);
#endif
