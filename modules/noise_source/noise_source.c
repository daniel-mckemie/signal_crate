#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <ncurses.h>

#include "noise_source.h"
#include "pink_filter.h"
#include "module.h"
#include "util.h"

static void noise_source_process(Module* m, float* in, unsigned long frames) {
	NoiseSource *state = (NoiseSource*)m->state;

	float amp;
	NoiseType noise_type;

	pthread_mutex_lock(&state->lock);
	amp = process_smoother(&state->smooth_amp, state->amplitude);
	noise_type = state->noise_type;
	pthread_mutex_unlock(&state->lock);

	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);

		if (strcmp(param, "amp") == 0) {
			amp *= control;
		}
	}

	
	for (unsigned long i=0; i<frames; i++) {
		float white = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
		float value = 0.0f;
		switch (noise_type) {
			case WHITE_NOISE:	value = white; break;
			case PINK_NOISE:	value = pink_filter_process(&state->pink, white); break;
			case BROWN_NOISE:	value = brown_noise_process(&state->brown, white); break;

		}
		m->output_buffer[i] = amp * value;
	}
}

static void noise_source_draw_ui(Module *m, int y, int x) {
	NoiseSource *state = (NoiseSource*)m->state;
	const char *noise_names[] = {"White", "Pink", "Brown"};

	float amp;
	NoiseType noise_type;
	
	pthread_mutex_lock(&state->lock);
	amp = process_smoother(&state->smooth_amp, state->amplitude);
	noise_type = state->noise_type;
	pthread_mutex_unlock(&state->lock);

	mvprintw(y, x, "[Noise Source] Amp: %.2f Hz, Type: %s", amp, noise_names[noise_type]);
	mvprintw(y+1, x,   "Real-time keys: -/= (amp), n: (type)");
	mvprintw(y+2, x,   "Command mode: :1 [amp], :n [noise type]");
}

static void clamp_params(NoiseSource *state) {
	if (state->amplitude < 0.0f) state->amplitude = 0.0f;
	if (state->amplitude > 1.0f) state->amplitude = 1.0f;
}

static void noise_source_handle_input(Module *m, int key) {
	NoiseSource *state = (NoiseSource*)m->state;
	int handled = 0;

	pthread_mutex_lock(&state->lock);

	if (!state->entering_command) {
		switch (key) {
			case '=': state->amplitude += 0.01f; handled = 1; break;
			case '-': state->amplitude -= 0.01f; handled = 1; break;
			case 'n': state->noise_type = (state->noise_type + 1) % 3; handled = 1; break;
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
                if (type == '1') state->amplitude = val;
                else if (type == '2') state->noise_type = ((int)val) % 3;
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

static void noise_source_destroy(Module* m) {
	if (!m) return;
	NoiseSource* state = (NoiseSource*)m->state;
	if (state) {
		pthread_mutex_destroy(&state->lock);
		free(state);
	}
}

Module* create_module(float sample_rate) {
	NoiseSource *state = calloc(1, sizeof(NoiseSource));
	state->amplitude = 0.5f;
	state->noise_type = WHITE_NOISE;
	state->sample_rate = sample_rate;
	pink_filter_init(&state->pink, sample_rate);
	brown_noise_init(&state->brown);
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_amp, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "noise_source";
	m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
	m->process = noise_source_process;
	m->draw_ui = noise_source_draw_ui;
	m->handle_input = noise_source_handle_input;
	m->destroy = noise_source_destroy;
	return m;
}
