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
	
	float smoothed1 = process_smoother(&state->smooth_param1, p1);
	// smoothed2 compiles with warning because unused in audio callback
	float smoothed2 = process_smoother(&state->smooth_param2, p2);
		
	// Audio process goes in here, sine tone for example
	for (unsigned long i = 0; i < frameCount; i++) {
		// Outputs here
        out[i] = AMPLITUDE * sinf(state->phase);
        state->phase += TWO_PI * smoothed1 / state->sample_rate;
        if (state->phase >= TWO_PI) state->phase -= TWO_PI;
    }

	return paContinue;								  

}

// Function to set boundaries of audio parameters in a universal way
// Handles text input and keyboard interaction; called in ui.c
void clamp_params(AudioModuleName *state) {
	// Enter all parameter boundaries
	if (state->param1 < 0.01f) state->param1 = 0.01f;
	if (state->param1 > 20000.0f) state->param1 = 20000.0f;
	if (state->param2 < 0.01f) state->param2 = 0.01f;
	if (state->param2 > 3.0f) state->param2 = 3.0f;
}
