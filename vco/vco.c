#include <math.h>
#include "vco.h"
#include "util.h"

int audio_callback(const void *input, void *output,
		unsigned long frameCount,
		const PaStreamCallbackTimeInfo *timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData) {
	VCO *state = (VCO*)userData;
	float *out = (float*)output;

	// param names abbreviated, freq, amp, phs
	float freq, amp, phs;
	pthread_mutex_lock(&state->lock); // Lock thread
	freq = process_smoother(&state->smooth_freq, state->frequency);
	amp = process_smoother(&state->smooth_amp, state->amplitude);
	phs = process_smoother(&state->smooth_phase, state->phase);
	Waveform waveform = state->waveform;
	pthread_mutex_unlock(&state->lock); // Unlock thread

		
	// Audio process goes in here, sine tone for example
	for (unsigned long i = 0; i < frameCount; i++) {
		// Outputs here
		float value = 0.0f;
		switch(waveform) {
			case WAVE_SINE:
				value = sinf(phs);
				break;
			case WAVE_SAW:
				value = 2.0f * (phs / TWO_PI) - 1.0f;
				break;
			case WAVE_SQUARE:
				value = (sinf(phs) >= 0.0f) ? 1.0f : -1.0f;
				break;
			case WAVE_TRIANGLE:
				value = 2.0f * fabs(2.0f * (phs / TWO_PI) - 1.0f) - 1.0f;
				break;
		}

		out[i] = amp * value;

		phs += TWO_PI * freq / state->sample_rate;
		if (phs >= TWO_PI) phs -= TWO_PI;
		
    }
	state->phase = phs;

	return paContinue;								  
}

// Function to set boundaries of audio parameters in a universal way
// Handles text input and keyboard interaction; called in ui.c
void clamp_params(VCO *state) {
	// Enter all parameter boundaries
	if (state->frequency < 0.01f) state->frequency = 0.01f;
	if (state->frequency > 20000.0f) state->frequency = 20000.0f;
	if (state->amplitude < 0.0f) state->amplitude = 0.0f;
	if (state->amplitude > 1.0f) state->amplitude = 1.0f;
	if (state->phase < 0.0f) state->phase = 0.0f;
	if (state->phase > TWO_PI) state->phase = fmodf(state->phase, TWO_PI);
}
