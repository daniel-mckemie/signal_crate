#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "vco.h"
#include "util.h"
#include "module.h"

int audio_callback(const void *input, void *output,
                   unsigned long frameCount,
                   const PaStreamCallbackTimeInfo *timeInfo,
                   PaStreamCallbackFlags statusFlags,
                   void *userData) {
    VCO *state = (VCO*)userData;
    float *out = (float*)output;

    float freq, amp;
    Waveform waveform;

    pthread_mutex_lock(&state->lock);
    freq = process_smoother(&state->smooth_freq, state->frequency);
    amp = process_smoother(&state->smooth_amp, state->amplitude);
    waveform = state->waveform;
    pthread_mutex_unlock(&state->lock);

	static float phs = 0.0f;

    for (unsigned long i=0;i<frameCount;i++) {
        float value = 0.0f;

        switch (waveform) {
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

    return paContinue;
}

// === Modular Interface Wrappers ===

void vco_process(Module* self, float* input, float* output, unsigned long frames) {
    VCO *state = (VCO*)self->state;

    float freq, amp;
    Waveform waveform;

    pthread_mutex_lock(&state->lock);
    freq = process_smoother(&state->smooth_freq, state->frequency);
    amp = process_smoother(&state->smooth_amp, state->amplitude);
    waveform = state->waveform;
    pthread_mutex_unlock(&state->lock);

	static float phs = 0.0f;

    for (unsigned long i = 0; i < frames; ++i) {
        float value = 0.0f;

        switch (waveform) {
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

        output[i] = amp * value;	
		phs += TWO_PI * freq / state->sample_rate;
		if (phs >= TWO_PI) phs -= TWO_PI;
    }
}

void vco_set_param(Module* self, const char* param, float value) {
    VCO *state = (VCO*)self->state;
    pthread_mutex_lock(&state->lock);
    if (strcmp(param, "freq") == 0) {
        state->frequency = value;
    } else if (strcmp(param, "amp") == 0) {
        state->amplitude = value;
    }
    pthread_mutex_unlock(&state->lock);
}

void vco_connect_input(Module* self, int input_index, float* source) {
    // No input for VCO
}

Module* create_module(float sample_rate) {
    VCO* state = malloc(sizeof(VCO));
    memset(state, 0, sizeof(VCO));
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
    state->waveform = WAVE_SINE;
    state->frequency = 440.0f;
    state->amplitude = 1.0f;

    Module* m = malloc(sizeof(Module));
    m->name = "vco";
    m->state = state;
    m->process = vco_process;
    m->set_param = vco_set_param;
    m->connect_input = vco_connect_input;
    m->output = malloc(sizeof(float) * 512);  // Default buffer size
    return m;
}

