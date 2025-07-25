#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "moog_filter.h"
#include "module.h"
#include "util.h"

static void moog_filter_process(Module *m, float* in, unsigned long frames) {
	MoogFilter *state = (MoogFilter*)m->state;
	float co, res;
	FilterType filt_type;

	pthread_mutex_lock(&state->lock); // Lock thread
	co = process_smoother(&state->smooth_co, state->cutoff);
	res = process_smoother(&state->smooth_res, state->resonance);
	filt_type = state->filt_type;
	pthread_mutex_unlock(&state->lock); // Unlock thread
	
	float mod_depth = 1.0f;
	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;
		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, 0.0f), 1.0f);
		if (strcmp(param, "cutoff") == 0) {
			float mod_range = state->cutoff * mod_depth;
			co = state->cutoff + norm * mod_range;
		} else if (strcmp(param, "res") == 0) {
			float mod_range = (4.2f - state->resonance) * mod_depth;
			res = state->resonance + norm * mod_range;
		}
	}

	state->display_cutoff = co;
	state->display_resonance = res;

	float wc = 2.0f * M_PI * co / state->sample_rate;	
	float g = wc / (wc + 1.0f); // Scale to appropriate ladder behavior
	float k = res;
	for (unsigned long i=0; i<frames; i++) {
		float input_sample = in[i];

		if (!isfinite(input_sample)) input_sample = 0.0f;
		float x = tanhf(input_sample);                       // Input limiter

		x -= k * state->z[3];                         // Feedback line
		x = tanhf(x);								  // Soft saturation

		state->z[0] += g * (x - state->z[0]);
		state->z[1] += g * (state->z[0] - state->z[1]);
		state->z[2] += g * (state->z[1] - state->z[2]);
		state->z[3] += g * (state->z[2] - state->z[3]);

		float y;
		switch (filt_type) {
			case LOWPASS:
				y = tanhf(state->z[3]); break;	
			case HIGHPASS:
				y = tanhf(x - state->z[3]); break;
			case BANDPASS:
				y = tanhf(state->z[2] - state->z[3]); break;
			case NOTCH:
				y = tanhf(x - k * state->z[3]); break;		
			case RESONANT:
				y = tanhf(state->z[3] + k * (state->z[3] - state->z[2])); break;
		}
				
		m->output_buffer[i] = fminf(fmaxf(y, -1.0f), 1.0f);
	}
}

static void moog_filter_draw_ui(Module *m, int y, int x) {
    MoogFilter *state = (MoogFilter*)m->state;
	const char *filt_names[] = {"LP", "HP", "BP", "Notch", "Res"};

    float co, res;
	FilterType filt_type;

    pthread_mutex_lock(&state->lock);
    co = state->display_cutoff;
    res = state->display_resonance;
	filt_type = state->filt_type;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y, x, "[Moog Filter] Cutoff: %.2f, Res: %.2f, Type: %s", co, res, filt_names[filt_type]);
    mvprintw(y+1, x, "Real-time keys: -/= (cutoff), _/+ (resonance)");
    mvprintw(y+2, x, "Command mode: :1 [cutoff], :2 [resonance] f: [filter type]");
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
			case 'f': state->filt_type = (state->filt_type + 1) % 5; handled = 1; break; 			  
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
                else if (type == '3') state->filt_type = ((int)val) % 5;
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

static void moog_filter_set_osc_param(Module* m, const char* param, float value) {
    MoogFilter* state = (MoogFilter*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "cutoff") == 0) {
		// Expect 0.0–1.0 from slider, map to 20 Hz – 20000 Hz
		float min_hz = 20.0f;
		float max_hz = 20000.0f;
		float norm = fminf(fmaxf(value, 0.0f), 1.0f); // clamp 0–1
		float hz = min_hz * powf(max_hz / min_hz, norm);  // exponential mapping
        state->cutoff = hz;
    } else if (strcmp(param, "res") == 0) {
        state->resonance = value;
    } else if (strcmp(param, "type") == 0) {
		if (value > 0.5f) {
			state->filt_type = (FilterType)((state->filt_type + 1) % 5);
		}
    } else {
        fprintf(stderr, "[moog_filter] Unknown OSC param: %s\n", param);
    }

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
	state->filt_type = LOWPASS;
	state->sample_rate = sample_rate;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_co, 0.75f);
	init_smoother(&state->smooth_res, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "moog_filter";
	m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
	m->process = moog_filter_process;
	m->draw_ui = moog_filter_draw_ui;
	m->handle_input = moog_filter_handle_input;
	m->set_param = moog_filter_set_osc_param;
	m->destroy = moog_filter_destroy;
	return m;
}
