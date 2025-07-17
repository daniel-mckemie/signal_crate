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

    float phase, mod_freq, car_amp, mod_amp, sr;
    pthread_mutex_lock(&state->lock);
    phase = state->phase;
    mod_freq = process_smoother(&state->smooth_mod_freq, state->mod_freq);
    car_amp = process_smoother(&state->smooth_car_amp, state->car_amp);
    mod_amp = process_smoother(&state->smooth_mod_amp, state->mod_amp);
    sr = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);

		if (strcmp(param, "mod_freq") == 0) {
			float min_hz = 1.0f;
			float max_hz = 20000.0f;
			mod_freq = min_hz * powf(max_hz / min_hz, control);
		} else if (strcmp(param, "car_amp") == 0) {
			car_amp *= control;
		} else if (strcmp(param, "mod_amp") == 0) {
			mod_amp *= control;
		}
	}

	int idx;
    for (unsigned long i = 0; i < frames; i++) {
		idx = (int)(phase / TWO_PI * SINE_TABLE_SIZE) % SINE_TABLE_SIZE;
		float car = in[i];
        float mod = sine_table[idx];
        m->output_buffer[i] = (car_amp * car) * (mod_amp * mod);
        phase += TWO_PI * mod_freq / sr; 
        if (phase >= TWO_PI)
            phase -= TWO_PI;
    }

    pthread_mutex_lock(&state->lock);
    state->phase = phase;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(RingMod *state) {
    if (state->car_amp < 0.0f) state->car_amp = 0.0f;
    if (state->car_amp > 1.0f) state->car_amp = 1.0f;

    if (state->mod_amp < 0.0f) state->mod_amp = 0.0f;
    if (state->mod_amp > 1.0f) state->mod_amp = 1.0f;

	if (state->mod_freq < 1.0f) state->mod_freq = 1.0f;
    if (state->mod_freq > 20000.0f) state->mod_freq = 20000.0f;
}

static void ringmod_draw_ui(Module* m, int y, int x) {
    RingMod* state = (RingMod*)m->state;

    float mod_freq, car_amp, mod_amp;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    mod_freq = state->mod_freq;
    car_amp = state->car_amp;
    mod_amp = state->mod_amp;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[RingMod] mod_freq: %.2f Hz, CarAmp: %.2f, ModAmp: %.2f", mod_freq, car_amp, mod_amp);
    mvprintw(y+1, x, "Real-time keys: -/= (mod_freq), _/+ (ModAmp), [/] (CarAmp)");
    mvprintw(y+2, x, "Command mode: :1 [mod_freq], :2 [CarAmp], :3 [ModAmp]");
}

static void ringmod_handle_input(Module* m, int key) {
    RingMod* state = (RingMod*)m->state;
	int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->mod_freq += 0.05f; handled = 1; break;
            case '-': state->mod_freq -= 0.05f; handled = 1; break;
            case '+': state->car_amp += 0.05f; handled = 1; break;
            case '_': state->car_amp -= 0.05f; handled = 1; break;
            case ']': state->mod_amp += 0.05f; handled = 1; break;
			case '[': state->mod_amp -= 0.05f; handled = 1; break;
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
                if (type == '1') state->mod_freq = val;
                else if (type == '2') state->car_amp = val;
                else if (type == '3') state->mod_amp = val;
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
    state->mod_freq = 440.0f;
    state->car_amp = 1.0f;
    state->mod_amp = 1.0f;
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
	init_sine_table();
    init_smoother(&state->smooth_mod_freq, 0.75f);
    init_smoother(&state->smooth_car_amp, 0.75f);
    init_smoother(&state->smooth_mod_amp, 0.75f);
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
