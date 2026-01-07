#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <ncurses.h>

#include "noise.h"
#include "pink_filter.h"
#include "module.h"
#include "util.h"

static inline float rng_white(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return ((float)x / 4294967296.0f) * 2.0f - 1.0f; // [-1, 1)
}

static void noise_process(Module* m, float* in, unsigned long frames) {
	Noise *state = (Noise*)m->state;
	NoiseType noise_type;
	float* out = m->output_buffer;

	pthread_mutex_lock(&state->lock);
	float base_amp    = state->amplitude;
	noise_type = state->noise_type;
	pthread_mutex_unlock(&state->lock);

	float amp_s = process_smoother(&state->smooth_amp, base_amp);

	float disp_amp = amp_s;

	for (unsigned long i=0; i<frames; i++) {
		float amp = amp_s;

		for (int j=0; j<m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "amp") == 0) {
				amp += control;
			}
		}

		clampf(&amp, 0.0f, 1.0f);

		disp_amp = amp;
		
		float white = rng_white(&state->rng);
		float value = 0.0f;
		switch (noise_type) {
			case WHITE_NOISE:	value = white; break;
			case PINK_NOISE:	value = pink_filter_process(&state->pink, white); break;
			case BROWN_NOISE:	value = brown_noise_process(&state->brown, white); break;

		}
		out[i] = amp * value;
	}
	pthread_mutex_lock(&state->lock);
	state->display_amp = disp_amp;
	pthread_mutex_unlock(&state->lock);
}

static void clamp_params(Noise *state) {
    clampf(&state->amplitude, 0.0f, 1.0f);
}

static void noise_draw_ui(Module *m, int y, int x) {
	Noise *state = (Noise*)m->state;
	const char *noise_names[] = {"White", "Pink", "Brown"};

	float amp;
	NoiseType noise_type;
	
	pthread_mutex_lock(&state->lock);
	amp = state->display_amp;
	noise_type = state->noise_type;
	pthread_mutex_unlock(&state->lock);

	BLUE();
	mvprintw(y, x, "[Noise:%s] ", m->name);
	CLR();

	LABEL(2, "amp:");
	ORANGE(); printw(" %.2f | ", amp); CLR();
	
	LABEL(2, "type:");
	ORANGE(); printw(" %s", noise_names[noise_type]); CLR();

	YELLOW();
	mvprintw(y+1, x,   "Real-time keys: -/= (amp), n: (type)");
	mvprintw(y+2, x,   "Command mode: :1 [amp], :n [type]");
	BLACK();
}

static void noise_handle_input(Module *m, int key) {
	Noise *state = (Noise*)m->state;
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

static void noise_set_osc_param(Module* m, const char* param, float value) {
    Noise* state = (Noise*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "amp") == 0) {
        state->amplitude = fminf(fmaxf(value, 0.0f), 1.0f);
    } else if (strcmp(param, "type") == 0) {
        if (value > 0.5f) {
            state->noise_type = (NoiseType)((state->noise_type + 1) % 3);
        }
    } else {
        fprintf(stderr, "[noise] Unknown OSC param: %s\n", param);
    }

	clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void noise_destroy(Module* m) {
	Noise* state = (Noise*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float amplitude = 0.5f;
	NoiseType noise_type = WHITE_NOISE;
	if (args && strstr(args, "amp=")) {
        sscanf(strstr(args, "amp="), "amp=%f", &amplitude);
	}
	if (args && strstr(args, "type=")) {
        char noise_str[32] = {0};
        sscanf(strstr(args, "type="), "type=%31s", noise_str);

        if (strcmp(noise_str, "white") == 0) noise_type = WHITE_NOISE;
        else if (strcmp(noise_str, "pink") == 0) noise_type = PINK_NOISE;
        else if (strcmp(noise_str, "brown") == 0) noise_type = BROWN_NOISE;
        else fprintf(stderr, "[noise] Unknown type: '%s'\n", noise_str);
    }


	Noise *state = calloc(1, sizeof(Noise));
	state->amplitude = amplitude;
	state->noise_type = noise_type;
	state->sample_rate = sample_rate;
	state->rng = (uint32_t)time(NULL) ^ (uintptr_t)state;
	pink_filter_init(&state->pink, sample_rate);
	brown_noise_init(&state->brown);
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_amp, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "noise";
	m->state = state;
	m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->process = noise_process;
	m->draw_ui = noise_draw_ui;
	m->handle_input = noise_handle_input;
	m->set_param = noise_set_osc_param;
	m->destroy = noise_destroy;
	return m;
}
