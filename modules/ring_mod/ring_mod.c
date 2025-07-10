#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>
#include <string.h>

#include "ring_mod.h"
#include "module.h"
#include "util.h"

static void ringmod_process(Module* m, float* in, unsigned long frames) {
    RingMod* state = (RingMod*)m->state;

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
        float car = in ? in[i] : 0.0f;
        float mod = sine_table[idx];
        m->output_buffer[i] = (amp1 * car) * (amp2 * mod);
        phase += TWO_PI * freq / sr; 
        if (phase >= TWO_PI)
            phase -= TWO_PI;
    }

    pthread_mutex_lock(&state->lock);
    state->phase = phase;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(RingMod *state) {
    if (state->amp1 < 0.0f) state->amp1 = 0.0f;
    if (state->amp1 > 1.0f) state->amp1 = 1.0f;

    if (state->amp2 < 0.0f) state->amp2 = 0.0f;
    if (state->amp2 > 1.0f) state->amp2 = 1.0f;

	if (state->freq < 1.0f) state->freq = 1.0f;
    if (state->freq > 20000.0f) state->freq = 20000.0f;
}

static void ringmod_draw_ui(Module* m, int row) {
    RingMod* state = (RingMod*)m->state;

    float freq, amp1, amp2;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    freq = state->freq;
    amp1 = state->amp1;
    amp2 = state->amp2;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

    mvprintw(row,   2, "[RingMod] Mod Freq: %.2f Hz", freq);
    mvprintw(row+1, 2, "          CarAmp: %.2f", amp1);
    mvprintw(row+2, 2, "          ModAmp: %.2f", amp2);
    mvprintw(row+3, 2, "Real-time keys: -/= (freq), _/+ (ModAmp), [/] (CarAmp)");
    mvprintw(row+4, 2, "Command mode: :1 [freq], :2 [CarAmp], :3 [ModAmp]");
    if (state->entering_command)
        mvprintw(row+5, 2, "%s", cmd);
}

static void ringmod_handle_input(Module* m, int key) {
    RingMod* state = (RingMod*)m->state;
	int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->freq += 0.05f; break;
            case '-': state->freq -= 0.05f; break;
            case '+': state->amp1 += 0.05f; break;
            case '_': state->amp1 -= 0.05f; break;
            case ']': state->amp2 += 0.05f; break;
			case '[': state->amp2 -= 0.05f; break;
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

static void ringmod_destroy(Module* m) {
    if (!m) return;
    RingMod* state = (RingMod*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
    RingMod* state = calloc(1, sizeof(RingMod));
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
    m->name = "ring_mod";
    m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = ringmod_process;
    m->draw_ui = ringmod_draw_ui;
    m->handle_input = ringmod_handle_input;
    m->destroy = ringmod_destroy;
    return m;
}
