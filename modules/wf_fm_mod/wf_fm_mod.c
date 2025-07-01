#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "wf_fm_mod.h"
#include "module.h"
#include "util.h"

static void wf_fm_mod_process(Module *m, float* in, float* out, unsigned long frames) {
    WFFMMod *state = (WFFMMod*)m->state;

    float mf, idx, ft_mod, ft_car, blend;
    pthread_mutex_lock(&state->lock);
    mf = process_smoother(&state->smooth_freq, state->modulator_freq);
    idx = process_smoother(&state->smooth_index, state->index);
    ft_mod = process_smoother(&state->smooth_fold_mod, state->fold_threshold_mod);
    ft_car = process_smoother(&state->smooth_fold_car, state->fold_threshold_car);
    blend = process_smoother(&state->smooth_blend, state->blend);
    pthread_mutex_unlock(&state->lock);

    for (unsigned long i=0; i<frames; i++) {
        float mod_raw = sinf(2.0f * M_PI * state->modulator_phase);
        float fm = sinf(2.0f * M_PI * idx * mod_raw);
        float modulator = (1.0f - blend) * fm + blend * fold(fm, ft_mod);

        float carrier = (in != NULL) ? in[i] : 0.0f;
        float carrier_mix = (1.0f - blend) * carrier + blend * fold(carrier, ft_car);

        out[i] = modulator * carrier_mix;

        state->modulator_phase += mf / state->sample_rate;
        if (state->modulator_phase >= 1.0f)
            state->modulator_phase -= 1.0f;
    }
}

static void wf_fm_mod_draw_ui(Module *m, int row) {
	WFFMMod *state = (WFFMMod*)m->state;

	pthread_mutex_lock(&state->lock);

	mvprintw(row, 2, "[Wavefolding FM Mod] Mod Freq %.2f Hz", state->modulator_freq);
	mvprintw(row+1, 2, "		   Mod Index %.2f", state->index);
	mvprintw(row+2, 2, "		   Mod Fold Amt %.2f", state->fold_threshold_mod);
	mvprintw(row+3, 2, "		   Car (In) Fold Amt %.2f", state->fold_threshold_car);
	mvprintw(row+4, 2, "		   Blend Amt %.2f", state->blend);
	mvprintw(row+5, 2, "Real-time keys: -/= (mod freq), _/+ (idx), [/]/{/} (fold), p/o [blend]");
    mvprintw(row+6, 2, "Command mode: :1 [mod freq], :2 [idx], :3 [fold mod], :4 [fold car], 5 [blend]");

	pthread_mutex_unlock(&state->lock);
}

static void clamp_params(WFFMMod *state) {
	// Set boundaries for params
	if (state->fold_threshold_mod < 0.01f) state->fold_threshold_mod = 0.01f;
    if (state->fold_threshold_mod > 1.0f)  state->fold_threshold_mod = 1.0f;

    if (state->fold_threshold_car < 0.01f) state->fold_threshold_car = 0.01f;
    if (state->fold_threshold_car > 1.0f)  state->fold_threshold_car = 1.0f;

    if (state->blend < 0.01f) state->blend = 0.01f;
    if (state->blend > 1.0f)  state->blend = 1.0f;

    if (state->index < 0.01f) state->index = 0.01f;
}

static void wf_fm_mod_handle_input(Module *m, int key) {
	WFFMMod *state = (WFFMMod*)m->state;

	pthread_mutex_lock(&state->lock);

	if (!state->entering_command) {
		switch (key) {
			case '=': state->modulator_freq += 0.5f; break;
			case '-': state->modulator_freq -= 0.5f; break;	
            case '+': state->index += 0.01f; break;
			case '_': state->index -= 0.01f; break;
			case ']': state->fold_threshold_mod += 0.01f; break;
            case '[': state->fold_threshold_mod -= 0.01f; break;
			case '}': state->fold_threshold_car += 0.01f; break;
			case '{': state->fold_threshold_car -= 0.01f; break;
			case 'p': state->blend += 0.01f; break;
			case 'o': state->blend -= 0.01f; break;
			case ':':
				state->entering_command = true;
				memset(state->command_buffer, 0, sizeof(state->command_buffer));
				state->command_index = 0;
				break;
		}
	} else {
		if (key == '\n') {
			state->entering_command = false;
			char type;
			float val;
			if (sscanf(state->command_buffer, "%c %f", &type, &val) == 2) {
				if (type == '1') state->modulator_freq = val;
                else if (type == '2') state->index = val;
                else if (type == '3') state->fold_threshold_mod = val;
                else if (type == '4') state->fold_threshold_car = val;
				else if (type == '5') state->blend = val;
			}
        } else if (key == 27) {
            state->entering_command = false;
        } else if ((key == KEY_BACKSPACE || key == 127) && state->command_index > 0) {
			state->command_index--;
			state->command_buffer[state->command_index] = '\0';
		} else if (key >= 32 && key < 127 && state->command_index < sizeof(state->command_buffer) - 1) {
			state->command_buffer[state->command_index++] = (char)key;
			state->command_buffer[state->command_index] = '\0';
		}
	}
	clamp_params(state);
	pthread_mutex_unlock(&state->lock);
}

static void wf_fm_mod_destroy(Module* m) {
    if (!m) return;
    WFFMMod* state = (WFFMMod*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
	WFFMMod *state = calloc(1, sizeof(WFFMMod));
	state->modulator_freq = 440.0f;
	state->fold_threshold_mod = 0.5f;
	state->fold_threshold_car = 0.5f;
	state->blend = 0.5f;
	state->index = 1.0f;
	state->sample_rate = sample_rate;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_freq, 0.75f);	
	init_smoother(&state->smooth_index, 0.75f);
	init_smoother(&state->smooth_blend, 0.75f);
	init_smoother(&state->smooth_fold_mod, 0.75f );
	init_smoother(&state->smooth_fold_car, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "wf_fm_mod";
	m->state = state;
	m->process = wf_fm_mod_process;
	m->draw_ui = wf_fm_mod_draw_ui;
	m->handle_input = wf_fm_mod_handle_input;
	m->destroy = wf_fm_mod_destroy;
	return m;
}
