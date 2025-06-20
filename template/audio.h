#ifndef AUDIO_H
#define AUDIO_H
#include <pthread.h>
#include <portaudio.h>

#define AMPLITUDE 0.99f

// This is the core of the module, defining params
typedef struct {
	float param1, // Ex cutoff
	float param2,
	pthread_mutex_t lock; // Used for threading when creating a UI
	volatile int running; // Used to cleanly quit
} AudioModuleName

// define callback, should stay as-is
int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userdata);

#endif

