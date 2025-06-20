#include <math.h>
#include "audio.h"
#include "util.h"

int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo *timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData) {
	AudioModuleName *state = (AudioModuleName*)userData;
	const float *in = (const float*)input;
	float *out = (float*)output;

	// param names abbreviated
	float p1, p2;
	pthread_mutex_lock(&state->lock); // Lock thread
	p1 = state->param1;
	p2 = state->param2;
	pthread_mutex_unlock(&state->lock); // Unlock thread

	// Audio process goes in here
	for (unsigned long i=0; i<frameCount; i++) {
		// Sine Tone for example
		// Outputs here
		out[i] = AMPLITUDE * sinf(state->phase);
		state->phase += (float(TWO_PI * state->param1 / state->sample_rate);
				if (state->phase >= TWO_PI)
				state->phase -= TWO_PI;
	}

	return paContinue;
									  

}
