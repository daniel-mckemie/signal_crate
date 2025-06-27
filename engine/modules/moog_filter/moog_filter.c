#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "moog_filter.h"
#include "util.h"
#include "module.h"

int audio_callback(const void *input, void *output,
                   unsigned long frameCount,
                   const PaStreamCallbackTimeInfo *timeInfo,
                   PaStreamCallbackFlags statusFlags,
                   void *userData) {

    MoogFilter *state = (MoogFilter*)userData;
    const float *in = (const float*)input;
    float *out = (float*)output;

    float co, res;

    pthread_mutex_lock(&state->lock);
    co = process_smoother(&state->smooth_cutoff, state->cutoff);
    res = process_smoother(&state->smooth_resonance, state->resonance);
    pthread_mutex_unlock(&state->lock);

    float wc = 2.0f * M_PI * co / state->sample_rate;
    float g = wc / (wc + 1.0f);
    float k = res;

    for (unsigned long i = 0; i < frameCount; i++) {
        float x = tanhf(in[i]);
        x -= k * state->z[3];
        x = tanhf(x);

        state->z[0] += g * (x - state->z[0]);
        state->z[1] += g * (state->z[0] - state->z[1]);
        state->z[2] += g * (state->z[1] - state->z[2]);
        state->z[3] += g * (state->z[2] - state->z[3]);

        out[i] = tanhf(state->z[3]);
    }

    return paContinue;
}

// === Modular Interface Wrappers ===

void moog_process(Module* self, float* input, float* output, unsigned long frames) {
    MoogFilter* state = (MoogFilter*)self->state;

    float co, res;
    pthread_mutex_lock(&state->lock);
    co = process_smoother(&state->smooth_cutoff, state->cutoff);
    res = process_smoother(&state->smooth_resonance, state->resonance);
    pthread_mutex_unlock(&state->lock);

    float wc = 2.0f * M_PI * co / state->sample_rate;
    float g = wc / (wc + 1.0f);
    float k = res;

    for (unsigned long i = 0; i < frames; ++i) {
        float x = input ? tanhf(input[i]) : 0.0f;
        x -= k * state->z[3];
        x = tanhf(x);

        state->z[0] += g * (x - state->z[0]);
        state->z[1] += g * (state->z[0] - state->z[1]);
        state->z[2] += g * (state->z[1] - state->z[2]);
        state->z[3] += g * (state->z[2] - state->z[3]);

        output[i] = tanhf(state->z[3]);
    }
}

void moog_set_param(Module* self, const char* param, float value) {
    MoogFilter *state = (MoogFilter*)self->state;
    pthread_mutex_lock(&state->lock);
    if (strcmp(param, "cutoff") == 0) {
        state->cutoff = value;
    } else if (strcmp(param, "resonance") == 0) {
        state->resonance = value;
    }
    pthread_mutex_unlock(&state->lock);
}

void moog_connect_input(Module* self, int input_index, float* source) {
    // Placeholder for future buffer patching
}

Module* create_module(float sample_rate) {
    MoogFilter* state = malloc(sizeof(MoogFilter));
    memset(state, 0, sizeof(MoogFilter));
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
    state->cutoff = 1000.0f;
    state->resonance = 0.5f;

    Module* m = malloc(sizeof(Module));
    m->name = "moog_filter";
    m->state = state;
    m->process = moog_process;
    m->set_param = moog_set_param;
    m->connect_input = moog_connect_input;
    m->output = malloc(sizeof(float) * 512);
    return m;
}

