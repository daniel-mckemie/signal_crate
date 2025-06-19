#include <math.h>
#include "audio.h"
#include "util.h"

int audio_callback(const void *input, void *output,
                   unsigned long frameCount,
                   const PaStreamCallbackTimeInfo *timeInfo,
                   PaStreamCallbackFlags statusFlags,
                   void *userData) {
    FMState *state = (FMState*)userData;
    const float *in = (const float*)input;
    float *out = (float*)output;

    float mf, idx, ft_mod, ft_car, blend;
    pthread_mutex_lock(&state->lock);
    mf = state->modulator_freq;
    idx = state->index;
    ft_mod = state->fold_threshold_mod;
    ft_car = state->fold_threshold_car;
    blend = state->blend;
    pthread_mutex_unlock(&state->lock);


    for (unsigned long i = 0; i < frameCount; i++) {
        float mod_raw = (sinf(TWO_PI * state->modulator_phase) + 1.0f);
        float fm = sinf(TWO_PI * (idx * mod_raw));
        float modulator = (1.0f - blend) * fm + blend * fold(fm, ft_mod);

        float carrier = in[i];
        float carrier_mix = (1.0f - blend) * carrier + blend * fold(carrier, ft_car);

        out[i] = AMPLITUDE * modulator * carrier_mix;

        state->modulator_phase += mf / state->sample_rate;
        if (state->modulator_phase >= 1.0f) state->modulator_phase -= 1.0f;
    }

    return paContinue;
}

