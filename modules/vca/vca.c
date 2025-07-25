#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "module.h"
#include "util.h"
#include "vca.h"

static void output_process(Module* m, float* in, unsigned long frames) {
    OutputState* state = (OutputState*)m->state;
    float* out = m->output_buffer;
	float gain;

    pthread_mutex_lock(&state->lock);
	gain = state->gain;
    pthread_mutex_unlock(&state->lock);
	
	float mod_depth = 1.0f;
	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

		if (strcmp(param, "gain") == 0) {
			float mod_range = (1.0f - state->gain) * mod_depth;
			gain = state->gain + norm * mod_range;
		}
	}

	state->display_gain = gain;

	for (unsigned long i = 0; i < frames; i++) {
		float smoothed_gain = process_smoother(&state->smooth_gain, gain);

        out[i] = smoothed_gain * in[i];
    }
}

static void output_draw_ui(Module* m, int y, int x) {
    OutputState* state = (OutputState*)m->state;

    pthread_mutex_lock(&state->lock);
    float gain = state->display_gain;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[VCA:%s] Gain: %.2f", m->name, gain);
    mvprintw(y+1, x, "Real-time keys: -/= gain");
    mvprintw(y+2, x, "Command mode: :1 [gain]");
}

static void output_handle_input(Module* m, int key) {
    OutputState* state = (OutputState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '-': state->gain -= 0.01f; handled = 1; break;
            case '=': state->gain += 0.01f; handled = 1; break;
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
                if (type == '1') state->gain = val;
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

    // Clamp
    if (handled) {
        if (state->gain < 0.0f) state->gain = 0.0f;
        if (state->gain > 1.0f) state->gain = 1.0f;
		state->display_gain = state->gain;
    }

    pthread_mutex_unlock(&state->lock);
}

static void output_set_osc_param(Module* m, const char* param, float value) {
    OutputState* state = (OutputState*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "gain") == 0) {
        state->gain = fmaxf(value, 0.0f);
    } else {
        fprintf(stderr, "[output] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&state->lock);
}

static void output_destroy(Module* m) {
    OutputState* state = (OutputState*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(float sample_rate) {
    OutputState* state = calloc(1, sizeof(OutputState));
    state->gain = 1.0f;
    pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_gain, 0.75);

    Module* m = calloc(1, sizeof(Module));
    m->name = "output";  // IMPORTANT: engine uses "out" for final audio
    m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));  // mono
    m->process = output_process;
    m->draw_ui = output_draw_ui;
    m->handle_input = output_handle_input;
    m->set_param = output_set_osc_param;
    m->destroy = output_destroy;
    return m;
}

