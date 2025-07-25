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

    float phase, freq, car_amp, depth, sr;
    pthread_mutex_lock(&state->lock);
    phase = state->phase;
    freq = process_smoother(&state->smooth_freq, state->freq);
    car_amp = process_smoother(&state->smooth_car_amp, state->car_amp);
    depth = process_smoother(&state->smooth_depth, state->depth);
    sr = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

	// Non-destructive control input
	float mod_depth = 1.0f;
	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

		if (strcmp(param, "freq") == 0) {
			float mod_range = state->freq * mod_depth;
			freq = state->freq + norm * mod_range; 
		} else if (strcmp(param, "car_amp") == 0) {
			float mod_range = (1.0f - state->car_amp) * mod_depth;
			car_amp = state->car_amp + norm * mod_range;
		} else if (strcmp(param, "depth") == 0) {
			float mod_range = (1.0f - state->depth) * mod_depth;
			depth = state->depth + norm * mod_range;

		}
	}

	state->display_freq = freq;
	state->display_car_amp = car_amp;
	state->display_depth = depth;

	int idx;
    for (unsigned long i = 0; i < frames; i++) {
		idx = (int)(phase / TWO_PI * SINE_TABLE_SIZE) % SINE_TABLE_SIZE;
		float car = in[i]; 
        float mod = sine_table[idx]; 
		float unipolar_mod = (depth * mod + 1.0f) * 0.5f; // Now [0, depth]
        m->output_buffer[i] = (car_amp * car) * unipolar_mod;
        phase += TWO_PI * freq / sr; 
        if (phase >= TWO_PI)
            phase -= TWO_PI;
    }

    pthread_mutex_lock(&state->lock);
    state->phase = phase;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(AmpMod *state) {
    if (state->car_amp < 0.0f) state->car_amp = 0.0f;
    if (state->car_amp > 1.0f) state->car_amp = 1.0f;

    if (state->depth < 0.0f) state->depth = 0.0f;
    if (state->depth > 1.0f) state->depth = 1.0f;
    if (state->freq < 0.01f) state->freq = 0.01f;
	if (state->freq > state->sample_rate * 0.45f) state->freq = state->sample_rate * 0.45f;
}

static void ampmod_draw_ui(Module* m, int y, int x) {
    AmpMod* state = (AmpMod*)m->state;

    float freq, car_amp, depth;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    freq = state->display_freq;
    car_amp = state->display_car_amp;
    depth = state->display_depth;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[AmpMod] Freq: %.2f Hz, Car_Amp: %.2f, Depth: %.2f", freq, car_amp, depth);
    mvprintw(y+1, x, "Real-time keys: -/= (freq), _/+ (Car_Amp), [/] (Depth)");
    mvprintw(y+2, x, "Command mode: :1 [freq], :2 [car_amp], :3 [depth]");
}

static void ampmod_handle_input(Module* m, int key) {
    AmpMod* state = (AmpMod*)m->state;
	int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->freq += 0.05f; handled = 1; break;
            case '-': state->freq -= 0.05f; handled = 1; break;
            case '+': state->car_amp += 0.05f; handled = 1; break;
            case '_': state->car_amp -= 0.05f; handled = 1; break;
            case ']': state->depth += 0.05f; handled = 1; break;
			case '[': state->depth -= 0.05f; handled = 1; break;
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
                else if (type == '2') state->car_amp = val;
                else if (type == '3') state->depth = val;
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

static void amp_mod_set_osc_param(Module* m, const char* param, float value) {
    AmpMod* state = (AmpMod*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "freq") == 0) {
        // Expect 0.0–1.0 mapped to 0.1 Hz – 20 Hz for modulation rate
        float min_hz = 0.1f;
        float max_hz = 20000.0f;
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        float hz = min_hz * powf(max_hz / min_hz, norm);
        state->freq = hz;
    } else if (strcmp(param, "car_amp") == 0) {
        state->car_amp = fminf(fmaxf(value, 0.0f), 1.0f);
    } else if (strcmp(param, "depth") == 0) {
        state->depth = fminf(fmaxf(value, 0.0f), 1.0f);
    } else {
        fprintf(stderr, "[amp_mod] Unknown OSC param: %s\n", param);
    }

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
    state->car_amp = 1.0f;
    state->depth = 1.0f;
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
	init_sine_table();
    init_smoother(&state->smooth_freq, 0.75f);
    init_smoother(&state->smooth_car_amp, 0.75f);
    init_smoother(&state->smooth_depth, 0.75f);
    clamp_params(state);

    Module* m = calloc(1, sizeof(Module));
    m->name = "amp_mod";
    m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = ampmod_process;
    m->draw_ui = ampmod_draw_ui;
    m->handle_input = ampmod_handle_input;
	m->set_param = amp_mod_set_osc_param;
    m->destroy = ampmod_destroy;
    return m;
}
