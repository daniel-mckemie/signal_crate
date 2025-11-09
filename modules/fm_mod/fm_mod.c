#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <pthread.h>
#include <ncurses.h>

#include "fm_mod.h"
#include "module.h"
#include "util.h"

static void fm_mod_process(Module *m, float* in, unsigned long frames) {
    FMMod *state = (FMMod*)m->state;

    float mf, car_amp, mod_amp, idx;
    pthread_mutex_lock(&state->lock);
    mf = process_smoother(&state->smooth_freq, state->mod_freq);
    car_amp = process_smoother(&state->smooth_car_amp, state->car_amp);
    mod_amp = process_smoother(&state->smooth_mod_amp, state->mod_amp);
    idx = process_smoother(&state->smooth_index, state->index);
    pthread_mutex_unlock(&state->lock);

	// --- Control Modulation Block ---
	float mod_depth = 1.0f;
    for (int i = 0; i < m->num_control_inputs; i++) {
        if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

        const char* param = m->control_input_params[i];
        float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "mod_freq") == 0) {
			float mod_range = state->mod_freq * mod_depth;
			mf = state->mod_freq + norm * mod_range;
        } else if (strcmp(param, "car_amp") == 0) {
			float mod_range = (1.0f - state->car_amp) * mod_depth;
            car_amp = state->car_amp + norm * mod_range;
        } else if (strcmp(param, "mod_amp") == 0) {
			float mod_range = (1.0f - state->mod_amp) * mod_depth;
            mod_amp = state->mod_amp + norm * mod_range;
        } else if (strcmp(param, "idx") == 0) {
			float mod_range = (10.0f - state->index) * mod_depth;
            idx = state->index + norm * mod_range;
		}
    }

	state->display_freq = mf;
	state->display_car_amp = car_amp;
	state->display_mod_amp = mod_amp;
	state->display_index = idx;
	
    for (unsigned long i=0; i<frames; i++) {
		float input = in[i];
        float mod = sinf(2.0f * M_PI * state->modulator_phase);
        float fm = sinf(2.0f * M_PI * (idx + mod_amp * mod));
		m->output_buffer[i] = car_amp * input * fm;

        state->modulator_phase += mf / state->sample_rate;
        if (state->modulator_phase >= 1.0f)
            state->modulator_phase -= 1.0f;
    }
}

static void clamp_params(FMMod *state) {
	clampf(&state->index, 0.01f, FLT_MAX);
	clampf(&state->mod_freq, 0.01f, state->sample_rate * 0.45f);
	clampf(&state->car_amp, 0.0f, 1.0f);
	clampf(&state->mod_amp, 0.0f, 1.0f);
}

static void fm_mod_draw_ui(Module *m, int y, int x) {
    FMMod *state = (FMMod*)m->state;
    float freq, car_amp, mod_amp, idx;

    pthread_mutex_lock(&state->lock);
    freq = state->display_freq;
    car_amp = state->display_car_amp;
    mod_amp = state->display_mod_amp;
    idx = state->display_index;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y, x, "[FMMod:%s] mod_freq %.2f Hz | car_amp %.2f | mod_amp %.2f | idx %.2f", m->name, freq, car_amp, mod_amp, idx);
    mvprintw(y+1, x, "Real-time keys: -/= (mod freq), _/+ (car_amp), {/} (mod_amp), [/] (idx)");
    mvprintw(y+2, x, "Command mode: :1 [mod freq], :2 [car amp], :3 [mod_amp], :4 [idx]"); 
}

static void fm_mod_handle_input(Module *m, int key) {
    FMMod *state = (FMMod*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->mod_freq += 0.5f; handled = 1; break;
            case '-': state->mod_freq -= 0.5f; handled = 1; break;	
            case '+': state->car_amp += 0.01f; handled = 1; break;
            case '_': state->car_amp -= 0.01f; handled = 1; break;
            case '}': state->mod_amp += 0.01f; handled = 1; break;
            case '{': state->mod_amp -= 0.01f; handled = 1; break;
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
                if (type == '1') state->mod_freq = val;
                else if (type == '2') state->car_amp= val;
                else if (type == '3') state->mod_amp = val;
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

static void fm_mod_set_osc_param(Module* m, const char* param, float value) {
    FMMod* state = (FMMod*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "mod_freq") == 0) {
        float min_hz = 0.01f;
        float max_hz = 20000.0f;
        float norm = fminf(fmaxf(value, 0.0f), 1.0f); // clamp
        float hz = min_hz * powf(max_hz / min_hz, norm);
        state->mod_freq = hz;
    } else if (strcmp(param, "car_amp") == 0) {
        state->car_amp = fmaxf(value, 0.0f);
    } else if (strcmp(param, "mod_amp") == 0) {
        state->mod_amp = fmaxf(value, 0.0f);
    } else if (strcmp(param, "index") == 0) {
        state->index = fmaxf(value, 0.0f);
	} else {
        fprintf(stderr, "[fm_mod] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&state->lock);
}

static void fm_mod_destroy(Module* m) {
    FMMod* state = (FMMod*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float mod_freq = 440.0f;
	float car_amp = 1.0f;
	float mod_amp = 1.0f;
	float index = 1.0f;
    if (args && strstr(args, "mod_freq=")) {
        sscanf(strstr(args, "mod_freq="), "mod_freq=%f", &mod_freq);
	}
	if (args && strstr(args, "idx=")) {
        sscanf(strstr(args, "idx="), "idx=%f", &index);
    }
	if (args && strstr(args, "car_amp=")) {
        sscanf(strstr(args, "car_amp="), "car_amp=%f", &car_amp);
    }
	if (args && strstr(args, "mod_amp=")) {
        sscanf(strstr(args, "mod_amp="), "mod_amp=%f", &mod_amp);
    }
	FMMod *state = calloc(1, sizeof(FMMod));
	state->mod_freq = mod_freq;
	state->car_amp = car_amp;
	state->mod_amp = mod_amp;
	state->index = index;
	state->sample_rate = sample_rate;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_freq, 0.75f);	
	init_smoother(&state->smooth_car_amp, 0.75f);	
	init_smoother(&state->smooth_mod_amp, 0.75f);	
	init_smoother(&state->smooth_index, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "fm_mod";
	m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
	m->process = fm_mod_process;
	m->draw_ui = fm_mod_draw_ui;
	m->handle_input = fm_mod_handle_input;
	m->set_param = fm_mod_set_osc_param;
	m->destroy = fm_mod_destroy;
	return m;
}

