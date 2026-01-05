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
	FilterType filt_type;
	float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
	float* out   = m->output_buffer;
	float* z = state->z; // filter stages

	pthread_mutex_lock(&state->lock);
	float base_co = state->cutoff;
	float base_res = state->resonance;
	float sample_rate = state->sample_rate;
	filt_type = state->filt_type;
	pthread_mutex_unlock(&state->lock);

	float co_s  = process_smoother(&state->smooth_co,  base_co);
	float res_s = process_smoother(&state->smooth_res, base_res);

	float disp_co  = co_s;
	float disp_res = res_s;
	
	for (unsigned long i=0; i<frames; i++) {
		float co  = co_s;
		float res = res_s;
		
		for (int j=0; j < m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "cutoff") == 0) {
				co += control * base_co;
			} else if (strcmp(param, "res") == 0) {
				res += control * 4.2f;
			}
		}

		clampf(&co, 10.0f, sample_rate * 0.45f);
		clampf(&res, 0.0f, 4.2f);

		disp_co  = co;
		disp_res = res;

		float wc = 2.0f * M_PI * co / sample_rate;	
		float g = wc / (wc + 1.0f); // Scale to appropriate ladder behavior
		float k = res;
		float in_s = input ? input[i] : 0.0f;

		if (!isfinite(in_s)) in_s = 0.0f;
		float x = tanhf(in_s); // Input limiter

		x -= k * z[3]; // Feedback line
		x = tanhf(x); // Soft saturation

		z[0] += g * (x - z[0]);
		z[1] += g * (z[0] - z[1]);
		z[2] += g * (z[1] - z[2]);
		z[3] += g * (z[2] - z[3]);

		float y;
		switch (filt_type) {
			case LOWPASS:
				y = tanhf(z[3]); break;	
			case HIGHPASS:
				y = tanhf(x - z[3]); break;
			case BANDPASS:
				y = tanhf(z[2] - z[3]); break;
			case NOTCH:
				y = tanhf(x - k * z[3]); break;		
			case RESONANT:
				y = tanhf(z[3] + k * (z[3] - z[2])); break;
		}
				
		float val = fminf(fmaxf(y, -1.0f), 1.0f);
		out[i] = val;
	}
	pthread_mutex_lock(&state->lock);
	state->display_cutoff = disp_co;
	state->display_resonance = disp_res;
	pthread_mutex_unlock(&state->lock);
}

static void clamp_params(MoogFilter *state) {
	clampf(&state->cutoff, 10.0f, state->sample_rate * 0.45f);
	clampf(&state->resonance, 0.0f, 4.2f);
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

	BLUE();
    mvprintw(y, x, "[Moog Filter:%s] ", m->name);
	CLR();

	LABEL(2, "cutoff:");
	ORANGE(); printw(" %.2f Hz | ", co); CLR();

	LABEL(2, "res:");
	ORANGE(); printw(" %.2f | ", res); CLR();

	LABEL(2, "filt_type:");
	ORANGE(); printw(" %s", filt_names[filt_type]); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= (cutoff), _/+ (res)");
    mvprintw(y+2, x, "Command mode: :1 [cutoff], :2 [res] f: [type]");
	BLACK();
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
		float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        state->resonance = norm * 4.2f;
    } else if (strcmp(param, "type") == 0) {
		if (value > 0.5f) {
			state->filt_type = (FilterType)((state->filt_type + 1) % 5);
		}
    } else {
        fprintf(stderr, "[moog_filter] Unknown OSC param: %s\n", param);
    }
	
	clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void moog_filter_destroy(Module* m) {
    MoogFilter* state = (MoogFilter*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float cutoff = 440.0f;
	float resonance = 1.0f;
	FilterType filt_type = LOWPASS;

	if (args && strstr(args, "cutoff=")) {
        sscanf(strstr(args, "cutoff="), "cutoff=%f", &cutoff);
    }
    if (args && strstr(args, "res=")) {
        sscanf(strstr(args, "res="), "res=%f", &resonance);
	}
	if (args && strstr(args, "type=")) {
        char filt_str[32] = {0};
        sscanf(strstr(args, "type="), "type=%31[^,]]", filt_str);

        if (strcmp(filt_str, "LP") == 0) filt_type = LOWPASS;
        else if (strcmp(filt_str, "HP") == 0) filt_type = HIGHPASS;
        else if (strcmp(filt_str, "BP") == 0) filt_type = BANDPASS;
        else if (strcmp(filt_str, "notch") == 0) filt_type = NOTCH;
        else if (strcmp(filt_str, "res") == 0) filt_type = RESONANT;
        else fprintf(stderr, "[moog_filter] Unknown type: '%s'\n", filt_str);
    }

	MoogFilter *state = calloc(1, sizeof(MoogFilter));
	state->cutoff = cutoff;
	state->resonance = resonance;
	state->filt_type = filt_type;
	state->sample_rate = sample_rate;
	state->z[0] = state->z[1] = state->z[2] = state->z[3] = 1e-6f;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_co, 0.75f);
	init_smoother(&state->smooth_res, 0.75f);
	clamp_params(state);

	Module *m = calloc(1, sizeof(Module));
	m->name = "moog_filter";
	m->state = state;
	m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->process = moog_filter_process;
	m->draw_ui = moog_filter_draw_ui;
	m->handle_input = moog_filter_handle_input;
	m->set_param = moog_filter_set_osc_param;
	m->destroy = moog_filter_destroy;
	return m;
}
