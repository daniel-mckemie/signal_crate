#include <math.h>
#include "audio.h"
#include "util.h"

int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo *timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData) {
	
	MoogFilter *state = (MoogFilter*)userData;
	const float *in = (const float*)input;
	float *out = (float*)output;

	// param names abbreviated
	float co, res;
	pthread_mutex_lock(&state->lock); // Lock thread
	co = state->cutoff;
	res = state->resonance;
	pthread_mutex_unlock(&state->lock); // Unlock thread
	
	float smoothed_co = process_smoother(&state->smooth_cutoff, co);
	float smoothed_res = process_smoother(&state->smooth_resonance, res);
	
	float wc = 2.0f * M_PI * smoothed_co / state->sample_rate;	
	float g = wc / (wc + 1.0f); // Scale to appropriate ladder behavior
	float k = smoothed_res;
	for (unsigned long i = 0; i < frameCount; i++) {
		float x = tanhf(in[i]);                       // Input limiter

		x -= k * state->z[3];                         // Feedback line
		x = tanhf(x);								  // Soft saturation

		state->z[0] += g * (x - state->z[0]);
		state->z[1] += g * (state->z[0] - state->z[1]);
		state->z[2] += g * (state->z[1] - state->z[2]);
		state->z[3] += g * (state->z[2] - state->z[3]);

		float y = tanhf(state->z[3]);				 // Output limiter
		out[i] = fminf(fmaxf(y, -1.0f), 1.0f);		 // Output saturation
		
	}
	return paContinue;								  

}

