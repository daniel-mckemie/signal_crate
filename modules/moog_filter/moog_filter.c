#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "moog_filter.h"
#include "module.h"
#include "util.h"

static void moog_filter_process(Module *m, float* in, float* out, unsigned long frames) {
	MoogFilter *state = (MoogFilter*)m->state;
	float co, res;

	pthread_mutex_lock(&state->lock); // Lock thread
	co = process_smoother(&state->smooth_co, state->cutoff);
	res = process_smoother(&state->smooth_res, state->resonance);
	pthread_mutex_unlock(&state->lock); // Unlock thread


	float wc = 2.0f * M_PI * co / state->sample_rate;	
	float g = wc / (wc + 1.0f); // Scale to appropriate ladder behavior
	float k = res;
	for (unsigned long i=0; i<frames; i++) {
		float input_sample = (in != NULL) ? in[i] : 0.0f; // prevent NULL deref
		if (!isfinite(input_sample)) input_sample = 0.0f;
		float x = tanhf(input_sample);                       // Input limiter

		x -= k * state->z[3];                         // Feedback line
		x = tanhf(x);								  // Soft saturation

		state->z[0] += g * (x - state->z[0]);
		state->z[1] += g * (state->z[0] - state->z[1]);
		state->z[2] += g * (state->z[1] - state->z[2]);
		state->z[3] += g * (state->z[2] - state->z[3]);

		float y = tanhf(state->z[3]);				 // Output limiter
		out[i] = fminf(fmaxf(y, -1.0f), 1.0f);		 // Output saturation

	}
}

static void moog_filter_draw_ui(Module *m, int row) {
    MoogFilter *state = (MoogFilter*)m->state;

    float co, res;

    pthread_mutex_lock(&state->lock);
    co = state->cutoff;
    res = state->resonance;
    pthread_mutex_unlock(&state->lock);

    mvprintw(row, 2, "[Moog Filter] Cutoff: %.2f", co);
    mvprintw(row+1, 2, "		Resonance: %.2f", res);
    mvprintw(row+2, 2, "Real-time keys: -/= (cutoff), _/+ (resonance)");
    mvprintw(row+3, 2, "Command mode: :1 [cutoff], :2 [resonance]");
}

static void clamp_params(MoogFilter *state) {
	// Set boundaries for params
	if (state->cutoff < 10.0f) state->cutoff = 10.0f;
	if (state->cutoff > state->sample_rate * 0.45f) state->cutoff = state->sample_rate * 0.45f;
	if (state->resonance < 0.0f) state->resonance = 0.0f;
	if (state->resonance > 4.2f) state->resonance = 4.2f;
}

static void moog_filter_handle_input(Module *m, int key) {
    MoogFilter *state = (MoogFilter*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->cutoff += 0.5f; handled = 1; break;
            case '-': state->cutoff -= 0.5f; handled = 1; break;
            case '+': state->resonance += 0.01f; handled = 1; break;
            case '_': state->resonance -= 0.01f; handled = 1; break;
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
                if (type == '1') state->cutoff = val;
                else if (type == '2') state->resonance = val;
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

static void moog_filter_destroy(Module* m) {
    if (!m) return;
    MoogFilter* state = (MoogFilter*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
	MoogFilter *state = calloc(1, sizeof(MoogFilter));
	state->cutoff = 440.0f;
	state->resonance = 0.5f;
	state->sample_rate = sample_rate;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_co, 0.75f);
	init_smoother(&state->smooth_res, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "moog_filter";
	m->state = state;
	m->process = moog_filter_process;
	m->draw_ui = moog_filter_draw_ui;
	m->handle_input = moog_filter_handle_input;
	m->destroy = moog_filter_destroy;
	return m;
}
