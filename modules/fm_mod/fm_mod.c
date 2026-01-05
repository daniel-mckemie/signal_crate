#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <pthread.h>
#include <ncurses.h>

#include "fm_mod.h"
#include "module.h"
#include "util.h"

static const float hilbert_taps[HILBERT_LEN] = {
	// Truncated Hilbert transformer...
    0.0f, -0.0062f, 0.0f, -0.0070f, 0.0f, -0.0081f, 0.0f, -0.0096f,
    0.0f, -0.0117f, 0.0f, -0.0148f, 0.0f, -0.0193f, 0.0f, -0.0260f,
    0.0f, -0.0366f, 0.0f, -0.0553f, 0.0f, -0.0967f, 0.0f, -0.3183f,
    0.0f,
     0.3183f, 0.0f, 0.0967f, 0.0f, 0.0553f, 0.0f, 0.0366f,
    0.0f, 0.0260f, 0.0f, 0.0193f, 0.0f, 0.0148f, 0.0f, 0.0117f,
    0.0f, 0.0096f, 0.0f, 0.0081f, 0.0f, 0.0070f, 0.0f, 0.0062f,
    0.0f
};

static void fm_mod_process(Module *m, float* in, unsigned long frames) {
    FMMod *state = (FMMod*)m->state;
	float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
	float* out = m->output_buffer;
	
    pthread_mutex_lock(&state->lock);
    float base_mod_freq = state->mod_freq;
    float base_car_amp  = state->car_amp;
    float base_mod_amp  = state->mod_amp;
    float base_idx      = state->index;
	float sr            = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

    float mod_freq_s = process_smoother(&state->smooth_freq, base_mod_freq);
    float car_amp_s = process_smoother(&state->smooth_car_amp, base_car_amp);
    float mod_amp_s = process_smoother(&state->smooth_mod_amp, base_mod_amp);
    float idx_s = process_smoother(&state->smooth_index, base_idx);

	float disp_mod_freq = mod_freq_s;
	float disp_car_amp = car_amp_s;
	float disp_mod_amp = mod_amp_s;
	float disp_idx = idx_s;

	for (unsigned long i=0; i<frames; i++) {
		float mod_freq = mod_freq_s;
		float car_amp  = car_amp_s;
		float mod_amp  = mod_amp_s;
		float idx      = idx_s;

		for (int j=0; j < m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "mod_freq") == 0) {
				mod_freq += control * base_mod_freq;
			} else if (strcmp(param, "car_amp") == 0) {
				car_amp += control;
			} else if (strcmp(param, "mod_amp") == 0) {
				mod_amp += control;
			} else if (strcmp(param, "idx") == 0) {
				idx += control * base_idx;
			}
		}

		clampf(&mod_freq, FM_MOD_MIN_FREQ, FM_MOD_MAX_FREQ);
		clampf(&car_amp, 0.0f, 1.0f);
		clampf(&mod_amp, 0.0f, 1.0f);
		clampf(&idx, 0.01f, FM_MOD_MAX_INDEX);

		disp_mod_freq = mod_freq;
		disp_car_amp = car_amp;
		disp_mod_amp = mod_amp;
		disp_idx = idx;

		
		state->hilbert_delay[state->hilbert_pos] = input[i];

		float imag = 0.0f;
		int p = state->hilbert_pos;
		for (int k = 0; k < HILBERT_LEN; k++) {
			imag += hilbert_taps[k] * state->hilbert_delay[p];
			if (--p < 0) p = HILBERT_LEN - 1;
		}

		float real = state->hilbert_delay[
			(state->hilbert_pos + HILBERT_LEN/2) % HILBERT_LEN
		];

		if (++state->hilbert_pos >= HILBERT_LEN)
			state->hilbert_pos = 0;

		/* carrier instantaneous phase */
		float carrier_phase = atan2f(imag, real);
		float mod = sinf(TWO_PI * state->modulator_phase);
		float phase = carrier_phase + idx * mod_amp * mod;
		out[i] = car_amp * cosf(phase);

		state->modulator_phase += mod_freq / sr;
		if (state->modulator_phase >= 1.0f)
			state->modulator_phase -= 1.0f;
	}

    pthread_mutex_lock(&state->lock);
    state->display_freq    = disp_mod_freq;
    state->display_car_amp = disp_car_amp;
    state->display_mod_amp = disp_mod_amp;
    state->display_index   = disp_idx;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(FMMod *state) {
	clampf(&state->index, 0.01f, 10.0f);
	clampf(&state->mod_freq, FM_MOD_MIN_FREQ, FM_MOD_MAX_FREQ);
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

	BLUE();
    mvprintw(y, x, "[FMMod:%s] ", m->name);
	CLR();

	LABEL(2, "mod_freq:");
	ORANGE(); printw(" %.2f Hz | ", freq); CLR();

	LABEL(2, "car_amp:");
	ORANGE(); printw(" %.2f | ", car_amp); CLR();

	LABEL(2, "mod_amp:");
	ORANGE(); printw(" %.2f | ", mod_amp); CLR();

	LABEL(2, "idx:");
	ORANGE(); printw(" %.2f", idx); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= (mod freq), _/+ (car_amp), {/} (mod_amp), [/] (idx)");
    mvprintw(y+2, x, "Command mode: :1 [mod freq], :2 [car amp], :3 [mod_amp], :4 [idx]"); 
	BLACK();
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
        float min_hz = FM_MOD_MIN_FREQ;
        float max_hz = FM_MOD_MAX_FREQ;
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        state->mod_freq = min_hz * powf(max_hz / min_hz, norm);
    } else if (strcmp(param, "car_amp") == 0) {
        state->car_amp = fmaxf(value, 0.0f);
    } else if (strcmp(param, "mod_amp") == 0) {
        state->mod_amp = fmaxf(value, 0.0f);
    } else if (strcmp(param, "index") == 0 || strcmp(param, "idx") == 0) {
		float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        state->index = norm * FM_MOD_MAX_INDEX;
	} else {
        fprintf(stderr, "[fm_mod] Unknown OSC param: %s\n", param);
    }
	clamp_params(state);
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
	m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->process = fm_mod_process;
	m->draw_ui = fm_mod_draw_ui;
	m->handle_input = fm_mod_handle_input;
	m->set_param = fm_mod_set_osc_param;
	m->destroy = fm_mod_destroy;
	return m;
}

