#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <ncurses.h>
#include <fftw3.h>

#include "spec_hold.h"
#include "module.h"
#include "util.h"

#define FFT_SIZE 2048
#define HOP_SIZE (FFT_SIZE / 2)

static float hann[FFT_SIZE];

static void init_hann_window() {
	for (int i=0; i<FFT_SIZE; i++) {
		hann[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
	}
}

static void spec_hold_process(Module* m, float* in, unsigned long frames) {
	SpecHold* state = (SpecHold*)m->state;

	for (unsigned long i = 0; i < frames; i++) {
		// Shift input buffer left by 1 sample and insert new input
		memmove(state->input_buffer, state->input_buffer + 1, sizeof(float) * (FFT_SIZE - 1));
		state->input_buffer[FFT_SIZE - 1] = in[i];
		state->hop_write_index++;

		if (state->hop_write_index >= HOP_SIZE) {
			state->hop_write_index = 0;

			// Apply Hann window
			for (int j = 0; j < FFT_SIZE; j++) {
				state->time_buffer[j] = state->input_buffer[j] * hann[j];
			}

			// FFT
			fftwf_execute(state->fft_plan);

			int bins = FFT_SIZE / 2 + 1;
			float nyquist = state->sample_rate / 2.0f;

			float raw_pivot, raw_tilt;

			pthread_mutex_lock(&state->lock);
			raw_pivot = state->pivot_hz;
			raw_tilt  = state->tilt;
			pthread_mutex_unlock(&state->lock);

			float pivot_hz = process_smoother(&state->smooth_pivot_hz, raw_pivot);
			float tilt     = process_smoother(&state->smooth_tilt, raw_tilt);

			float mod_depth = 1.0f;
			for (int i = 0; i < m->num_control_inputs; i++) {
				if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

				const char* param = m->control_input_params[i];
				float control = *(m->control_inputs[i]);
				float norm = fminf(fmaxf(control, -1.0f), 1.0f);

				if (strcmp(param, "pivot") == 0) {
					float mod_range = pivot_hz * mod_depth;
					pivot_hz = pivot_hz + norm * mod_range;
				} else if (strcmp(param, "tilt") == 0) {
					float mod_range = (1.0f - tilt) * mod_depth;
					tilt = tilt + norm * mod_range; 
				}
			}

			state->display_pivot = pivot_hz;
			state->display_tilt = tilt;

			for (int j = 0; j < bins; j++) {
				float mag, phase;

				if (!state->freeze) {
					mag = hypotf(state->freq_buffer[j][0], state->freq_buffer[j][1]);
					phase = atan2f(state->freq_buffer[j][1], state->freq_buffer[j][0]);

					state->frozen_mag[j] = mag;
					state->frozen_phase[j] = phase;
				} else {
					mag = state->frozen_mag[j];
					phase = state->frozen_phase[j];
				}

				// Always apply tilt
				float bin_hz = ((float)j / (float)bins) * nyquist;
				bin_hz = fmaxf(bin_hz, 1.0f);  // avoid log(0)
				float gain_db = tilt * 3.0f * log2f(bin_hz / pivot_hz);
				float gain = powf(10.0f, gain_db / 20.0f);

				state->freq_buffer[j][0] = gain * mag * cosf(phase);
				state->freq_buffer[j][1] = gain * mag * sinf(phase);
			}


			// IFFT
			fftwf_execute(state->ifft_plan);

			// Prevent DC buildup
			float dc = 0.0f;
			for (int j = 0; j < FFT_SIZE; j++) dc += state->time_buffer[j];
			dc /= FFT_SIZE;
			for (int j = 0; j < FFT_SIZE; j++) state->time_buffer[j] -= dc;

			// Window and overlap-add
			for (int j = 0; j < FFT_SIZE; j++) {
				float val = (state->time_buffer[j] * 0.5f) / FFT_SIZE;
				state->output_buffer[j] += val;
			}
		}
	}

	// Output frames
	memcpy(m->output_buffer, state->output_buffer, sizeof(float) * frames);
	memmove(state->output_buffer, state->output_buffer + frames, sizeof(float) * (FFT_SIZE - frames));
	memset(state->output_buffer + (FFT_SIZE - frames), 0, sizeof(float) * frames);
}

static void clamp_params(SpecHold* state) {
    clampf(&state->pivot_hz, 1.0f, state->sample_rate * 0.45f);
    clampf(&state->tilt, -1.0f, 1.0f);
}

static void spec_hold_draw_ui(Module* m, int y, int x) {
    SpecHold* state = (SpecHold*)m->state;

    float tilt;
	float pivot_hz;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    tilt = state->display_tilt;
	pivot_hz = state->display_pivot;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

	BLUE();
    mvprintw(y,   x, "[SpecTilt:%s] ", m->name);
	CLR();

	LABEL(2, "pivot:");
	ORANGE(); printw(" %.2f Hz | ", pivot_hz); CLR();

	LABEL(2, "tilt:");
	ORANGE(); printw(" %.2f | ", tilt); CLR();
	
	LABEL(2, "freeze:");
	ORANGE(); printw(" %s | ", state->freeze ? "ON" : "OFF"); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-time Keys: -/= tilt, _/+ pivot, [f] freeze");
    mvprintw(y+2, x, "Cmd Mode: :1 [pivot], :2 [tilt]");
	BLACK();
}

static void spec_hold_handle_input(Module* m, int key) {
    SpecHold* state = (SpecHold*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->pivot_hz += 0.01f; handled = 1; break;
            case '-': state->pivot_hz -= 0.01f; handled = 1; break;
            case '+': state->tilt += 0.01f; handled = 1; break;
            case '_': state->tilt -= 0.01f; handled = 1; break;
            case 'f': state->freeze = !state->freeze; handled = 1; break;
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
                if (type == '1') {state->pivot_hz = val;}
				else if (type == '2') {state->tilt = val;}
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

    if (handled) clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void spec_hold_set_osc_param(Module* m, const char* param, float value) {
    SpecHold* state = (SpecHold*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "tilt") == 0) {
        state->tilt = fminf(fmaxf(value * 2.0f - 1.0f, -1.0f), 1.0f);  // map [0,1] → [-1,1]
    } else if (strcmp(param, "pivot") == 0) {
        float min_hz = 20.0f;
        float max_hz = 20000.0f;
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        state->pivot_hz = min_hz * powf(max_hz / min_hz, norm);
    } else if (strcmp(param, "freeze") == 0) {
        if (value > 0.5f) state->freeze = !state->freeze;
    } else {
        fprintf(stderr, "[spec_hold] Unknown OSC param: %s\n", param);
    }

    clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void spec_hold_destroy(Module* m) {
    if (!m) return;
    SpecHold* state = (SpecHold*)m->state;
    if (state) {
        fftwf_destroy_plan(state->fft_plan);
        fftwf_destroy_plan(state->ifft_plan);
        fftwf_free(state->time_buffer);
        fftwf_free(state->freq_buffer);
        free(state->input_buffer);
        free(state->output_buffer);
        free(state->frozen_mag);
        free(state->frozen_phase);
        pthread_mutex_destroy(&state->lock);
        // Do NOT free(state) — destroy_base_module() will
    }
    destroy_base_module(m);
}


Module* create_module(const char* args, float sample_rate) {
	float tilt = 0.0f;
	float pivot_hz = 1000.0f;
	if (args && strstr(args, "tilt=")) {
        sscanf(strstr(args, "tilt="), "tilt=%f", &tilt);
    }
    if (args && strstr(args, "pivot=")) {
        sscanf(strstr(args, "pivot="), "pivot=%f", &pivot_hz);
	}

    SpecHold* state = calloc(1, sizeof(SpecHold));
    state->sample_rate = sample_rate;
    state->tilt = tilt;
	state->pivot_hz = pivot_hz;
    pthread_mutex_init(&state->lock, NULL);

    init_smoother(&state->smooth_tilt, 0.75f);
    init_smoother(&state->smooth_pivot_hz, 0.75f);
    clamp_params(state);

    init_hann_window();

    state->input_buffer = calloc(FFT_SIZE, sizeof(float));
    state->output_buffer = calloc(FFT_SIZE, sizeof(float));
    state->time_buffer = fftwf_alloc_real(FFT_SIZE);
    state->freq_buffer = fftwf_alloc_complex(FFT_SIZE / 2 + 1);
	state->frozen_mag = calloc(FFT_SIZE / 2 + 1, sizeof(float));
	state->frozen_phase = calloc(FFT_SIZE / 2 + 1, sizeof(float));
	state->freeze = false;
	memset(state->output_buffer, 0, sizeof(float) * FFT_SIZE);

    state->fft_plan = fftwf_plan_dft_r2c_1d(FFT_SIZE, state->time_buffer, state->freq_buffer, FFTW_ESTIMATE);
    state->ifft_plan = fftwf_plan_dft_c2r_1d(FFT_SIZE, state->freq_buffer, state->time_buffer, FFTW_ESTIMATE);

	state->hop_write_index = 0;

    Module* m = calloc(1, sizeof(Module));
    m->name = "spec_tilt";
    m->state = state;
    m->output_buffer = calloc(HOP_SIZE, sizeof(float));
    m->process = spec_hold_process;
    m->draw_ui = spec_hold_draw_ui;
    m->handle_input = spec_hold_handle_input;
	m->set_param = spec_hold_set_osc_param;
    m->destroy = spec_hold_destroy;
    return m;
}

