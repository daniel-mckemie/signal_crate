#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <pthread.h>
#include <ncurses.h>

#include "pm_mod.h"
#include "module.h"
#include "util.h"

static void pm_mod_process(Module *m, float* in, unsigned long frames) {
    PMMod *state = (PMMod*)m->state;

	float* in_car = m->inputs[0];
	float* in_mod = m->inputs[1];
	float* out = m->output_buffer;

	if (!in_car || !in_mod) {
		memset(out, 0, frames * sizeof(float));
		return;
	}

    pthread_mutex_lock(&state->lock);
    float car_amp = process_smoother(&state->smooth_car_amp, state->car_amp);
    float mod_amp = process_smoother(&state->smooth_mod_amp, state->mod_amp);
    float base_freq = process_smoother(&state->smooth_base_freq, state->base_freq);
    float idx = process_smoother(&state->smooth_index, state->index);
	float sr = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

	// --- Control Modulation Block ---
	float mod_depth = 1.0f;
    for (int i = 0; i < m->num_control_inputs; i++) {
        if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

        const char* param = m->control_input_params[i];
        float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "mod_amp") == 0) {
			float mod_range = (1.0f - state->mod_amp) * mod_depth; 
			mod_amp = state->mod_amp + norm * mod_range;
		} else if (strcmp(param, "car_amp") == 0) {
			float mod_range = (1.0f - state->car_amp) * mod_depth;
			car_amp = state->car_amp + norm * mod_range;
        } else if (strcmp(param, "idx") == 0) {
			float mod_range = (10.0f - state->index) * mod_depth;
            idx = state->index + norm * mod_range;
        } else if (strcmp(param, "base_freq") == 0) {
			float mod_range = state->base_freq * mod_depth;
			base_freq = state->base_freq + norm * mod_range;
		}
    }

	mod_amp = fminf(fmaxf(mod_amp, 0.0f), 1.0f);
	car_amp = fminf(fmaxf(car_amp, 0.0f), 1.0f);
	idx = fminf(fmaxf(idx, 0.0f), 10.0f);

	// Audio core
	float phase = state->modulator_phase;
    for (unsigned long i=0; i<frames; i++) {
		float car = in_car[i] * car_amp;
        float mod = in_mod[i] * mod_amp;

		float fm = car * sinf(phase + idx * mod);

		phase += TWO_PI * base_freq / sr;
		if (phase >= TWO_PI) phase -= TWO_PI;

		m->output_buffer[i] = fm;
    }
	state->modulator_phase = phase;

	state->display_mod_amp = mod_amp;
	state->display_car_amp = car_amp;
	state->display_base_freq = base_freq;
	state->display_index = idx;
}

static void clamp_params(PMMod *state) {
	clampf(&state->car_amp, 0.0f, 1.0f);
	clampf(&state->mod_amp, 0.0f, 1.0f);
	clampf(&state->base_freq, 0.01f, state->sample_rate * 0.45f);
	clampf(&state->index, 0.01f, FLT_MAX);
}

static void pm_mod_draw_ui(Module *m, int y, int x) {
    PMMod *state = (PMMod*)m->state;

	float car_amp, mod_amp, base_freq, idx;

    pthread_mutex_lock(&state->lock);
    car_amp = state->display_car_amp;
    mod_amp = state->display_mod_amp;
	base_freq = state->display_base_freq;
    idx = state->display_index;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y, x, "[PMMod:%s] car_amp %.2f | mod_amp %.2f | base_freq %.2f Hz | idx %.2f", m->name, car_amp, mod_amp, base_freq, idx);
    mvprintw(y+1, x, "Real-time keys: -/= (car amp), _/+ (mod amp), {/} (base_freq) [/] (idx)");
    mvprintw(y+2, x, "Command mode: :1 [car amp], :2 [mod amp], :3 [base_freq], :4 [idx]"); 
}

static void pm_mod_handle_input(Module *m, int key) {
    PMMod *state = (PMMod*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->car_amp += 0.01f; handled = 1; break;
            case '-': state->car_amp -= 0.01f; handled = 1; break;	
            case '+': state->mod_amp += 0.01f; handled = 1; break;
            case '_': state->mod_amp -= 0.01f; handled = 1; break;	
            case '}': state->base_freq += 0.5f; handled = 1; break;	
            case '{': state->base_freq -= 0.5f; handled = 1; break;	
            case ']': state->index += 0.01f; handled = 1; break;
            case '[': state->index -= 0.01f; handled = 1; break;
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
                if (type == '1') state->car_amp = val;
                else if (type == '2') state->mod_amp = val;
                else if (type == '3') state->base_freq = val;
                else if (type == '4') state->index = val;
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

static void pm_mod_set_osc_param(Module* m, const char* param, float value) {
    PMMod* state = (PMMod*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "car_amp") == 0) {
        state->car_amp = fminf(fmaxf(value, 0.0f), 1.0f);
    } else if (strcmp(param, "mod_amp") == 0) {
		state->mod_amp = fminf(fmaxf(value, 0.0f), 1.0f);
    } else if (strcmp(param, "index") == 0) {
        state->index = fmaxf(value, 0.0f);
	} else if (strcmp(param, "base_freq") == 0) {
		float min_hz = 0.01f;
		float max_hz = 20000.0f;
		float norm = fminf(fmaxf(value, 0.0f), 1.0f);
		float hz = min_hz * powf(max_hz / min_hz, norm);
		state->base_freq = hz;
    } else {
        fprintf(stderr, "[pm_mod] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&state->lock);
}

static void pm_mod_destroy(Module* m) {
    PMMod* state = (PMMod*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float car_amp = 1.0f;
	float mod_amp = 1.0f;
	float base_freq = 440.0f;
	float index = 1.0f;

    if (args && strstr(args, "car_amp="))
        sscanf(strstr(args, "car_amp="), "car_amp=%f", &car_amp);
    if (args && strstr(args, "mod_amp="))
        sscanf(strstr(args, "mod_amp="), "mod_amp=%f", &mod_amp);
    if (args && strstr(args, "base_freq="))
        sscanf(strstr(args, "base_freq="), "base_freq=%f", &base_freq);
	if (args && strstr(args, "idx="))
        sscanf(strstr(args, "idx="), "idx=%f", &index);

	PMMod *state = calloc(1, sizeof(PMMod));
	state->car_amp = car_amp;
	state->mod_amp = mod_amp;
	state->base_freq = base_freq;
	state->index = index;
	state->sample_rate = sample_rate;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_car_amp, 0.75f);	
	init_smoother(&state->smooth_mod_amp, 0.75f);	
	init_smoother(&state->smooth_base_freq, 0.75f);	
	init_smoother(&state->smooth_index, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "pm_mod";
	m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
	m->process = pm_mod_process;
	m->draw_ui = pm_mod_draw_ui;
	m->handle_input = pm_mod_handle_input;
	m->set_param = pm_mod_set_osc_param;
	m->destroy = pm_mod_destroy;
	return m;
}

