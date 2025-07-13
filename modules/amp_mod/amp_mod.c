#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>
#include <string.h>

#include "amp_mod.h"
#include "module.h"
#include "util.h"

static void ampmod_process(Module* m, float* in, unsigned long frames) {
    AmpMod* state = (AmpMod*)m->state;

    float phase, freq, amp1, amp2, sr;
    pthread_mutex_lock(&state->lock);
    phase = state->phase;
    freq = process_smoother(&state->smooth_freq, state->freq);
    amp1 = process_smoother(&state->smooth_amp1, state->amp1);
    amp2 = process_smoother(&state->smooth_amp2, state->amp2);
    sr = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

	int idx;
    for (unsigned long i = 0; i < frames; i++) {
		idx = (int)(phase / TWO_PI * SINE_TABLE_SIZE) % SINE_TABLE_SIZE;
		float car = in[i]; 
        float mod = sine_table[idx]; 
		float unipolar_mod = (amp2 * mod + 1.0f) * 0.5f; // Now [0, amp2]
        m->output_buffer[i] = (amp1 * car) * unipolar_mod;
        phase += TWO_PI * freq / sr; 
        if (phase >= TWO_PI)
            phase -= TWO_PI;
    }

    pthread_mutex_lock(&state->lock);
    state->phase = phase;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(AmpMod *state) {
    if (state->amp1 < 0.0f) state->amp1 = 0.0f;
    if (state->amp1 > 1.0f) state->amp1 = 1.0f;

    if (state->amp2 < 0.0f) state->amp2 = 0.0f;
    if (state->amp2 > 1.0f) state->amp2 = 1.0f;

    if (state->freq < 1.0f) state->freq = 1.0f;
    if (state->freq > 20000.0f) state->freq = 20000.0f;
}

static void ampmod_draw_ui(Module* m, int y, int x) {
    AmpMod* state = (AmpMod*)m->state;

    float freq, amp1, amp2;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    freq = state->freq;
    amp1 = state->amp1;
    amp2 = state->amp2;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[AmpMod] Mod Freq: %.2f Hz", freq);
    mvprintw(y+1, x, "          Car Amp: %.2f", amp1);
    mvprintw(y+2, x, "          Depth: %.2f", amp2);
    mvprintw(y+3, x, "Real-time keys: -/= (freq), _/+ (Car Amp), [/] (Depth)");
    mvprintw(y+4, x, "Command mode: :1 [freq], :2 [Car Amp], :3 [Depth]");
    if (state->entering_command)
        mvprintw(y+5, x, "%s", cmd);
}

static void ampmod_handle_input(Module* m, int key) {
    AmpMod* state = (AmpMod*)m->state;
	int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->freq += 0.05f; handled = 1; break;
            case '-': state->freq -= 0.05f; handled = 1; break;
            case '+': state->amp1 += 0.05f; handled = 1; break;
            case '_': state->amp1 -= 0.05f; handled = 1; break;
            case ']': state->amp2 += 0.05f; handled = 1; break;
			case '[': state->amp2 -= 0.05f; handled = 1; break;
            case ':':
                state->entering_command = true;
                memset(state->command_buffer, 0, sizeof(state->command_buffer));
                state->command_index = 0;
				handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            state->entering_command = false;
            char type;
            float val;
            if (sscanf(state->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') state->freq = val;
                else if (type == '2') state->amp1 = val;
                else if (type == '3') state->amp2 = val;
            }
			handled = 1;
        } else if (key == 27) {
            state->entering_command = false;
			handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && state->command_index > 0) {
            state->command_index--;
            state->command_buffer[state->command_index] = '\0';
			handled = 1;
        } else if (key >= 32 && key < 127 && state->command_index < sizeof(state->command_buffer) - 1) {
            state->command_buffer[state->command_index++] = (char)key;
            state->command_buffer[state->command_index] = '\0';
			handled = 1; 
        }
    }

	if (handled)
		clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void ampmod_destroy(Module* m) {
    if (!m) return;
    AmpMod* state = (AmpMod*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
    AmpMod* state = calloc(1, sizeof(AmpMod));
    state->freq = 440.0f;
    state->amp1 = 1.0f;
    state->amp2 = 1.0f;
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
	init_sine_table();
    init_smoother(&state->smooth_freq, 0.75f);
    init_smoother(&state->smooth_amp1, 0.75f);
    init_smoother(&state->smooth_amp2, 0.75f);
    clamp_params(state);

    Module* m = calloc(1, sizeof(Module));
    m->name = "amp_mod";
    m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = ampmod_process;
    m->draw_ui = ampmod_draw_ui;
    m->handle_input = ampmod_handle_input;
    m->destroy = ampmod_destroy;
    return m;
}
