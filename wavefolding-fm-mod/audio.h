#ifndef AUDIO_H
#define AUDIO_H
#include <pthread.h>
#include <portaudio.h>

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
} FMState;

int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userdata);

#endif
