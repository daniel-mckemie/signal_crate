#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "vco.h"
#include "module.h"
#include "util.h"
#include "vco_ui.h"

static void vco_process(Module *m, float* in, float* out, unsigned long frames) {
    VCO *state = (VCO*)m->state;
    float freq, amp;
    Waveform waveform;

    pthread_mutex_lock(&state->lock);
    freq = process_smoother(&state->smooth_freq, state->frequency);
    amp = process_smoother(&state->smooth_amp, state->amplitude);
    waveform = state->waveform;
    pthread_mutex_unlock(&state->lock);

    static float phs = 0.0f;
    for (unsigned long i = 0; i < frames; i++) {
        float value = 0.0f;
        switch (waveform) {
            case WAVE_SINE:     value = sinf(phs); break;
            case WAVE_SAW:      value = 2.0f * (phs / TWO_PI) - 1.0f; break;
            case WAVE_SQUARE:   value = (sinf(phs) >= 0.0f) ? 1.0f : -1.0f; break;
            case WAVE_TRIANGLE: value = 2.0f * fabs(2.0f * (phs / TWO_PI) - 1.0f) - 1.0f; break;
        }
        out[i] = amp * value;
        phs += TWO_PI * freq / state->sample_rate;
        if (phs >= TWO_PI) phs -= TWO_PI;
    }
}

static void vco_draw_ui(Module *m, int row) {
    VCO *state = (VCO*)m->state;
    const char *wave_names[] = {"Sine", "Saw", "Square", "Triangle"};
    mvprintw(row, 2,   "[VCO] Freq: %.2f Hz", state->frequency);
    mvprintw(row+1, 2, "      Amp : %.2f", state->amplitude);
    mvprintw(row+2, 2, "      Wave: %s", wave_names[state->waveform]);
}

static void clamp_params(VCO *state) {
    if (state->frequency < 0.01f) state->frequency = 0.01f;
    if (state->frequency > 20000.0f) state->frequency = 20000.0f;
    if (state->amplitude < 0.0f) state->amplitude = 0.0f;
    if (state->amplitude > 1.0f) state->amplitude = 1.0f;
}

Module* create_module(float sample_rate) {
    VCO *state = calloc(1, sizeof(VCO));
    state->frequency = 440.0f;
    state->amplitude = 0.5f;
    state->waveform = WAVE_SINE;
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
    init_smoother(&state->smooth_freq, 0.75f);
    init_smoother(&state->smooth_amp, 0.75f);
    clamp_params(state);

    Module *m = calloc(1, sizeof(Module));
    m->name = "vco";
    m->state = state;
    m->process = vco_process;
    m->draw_ui = vco_draw_ui;
    return m;
}
