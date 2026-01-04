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

static void spec_hold_process(Module* m, float* in, unsigned long frames)
{
    SpecHold* state = (SpecHold*)m->state;
    float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
    float* out   = m->output_buffer;

    /* Snapshot params */
    pthread_mutex_lock(&state->lock);
    float base_pivot  = state->pivot_hz;
    float base_tilt   = state->tilt;
    int   freeze      = state->freeze;
    float sample_rate = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

    float pivot_s = process_smoother(&state->smooth_pivot_hz, base_pivot);
    float tilt_s  = process_smoother(&state->smooth_tilt, base_tilt);

    float disp_pivot = pivot_s;
    float disp_tilt  = tilt_s;

    for (unsigned long i = 0; i < frames; i++) {
        float pivot = pivot_s;
        float tilt  = tilt_s;

        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "pivot") == 0) {
                pivot += control * base_pivot;
            } else if (strcmp(param, "tilt") == 0) {
                tilt += control * (1.0f - fabsf(base_tilt));
            }
        }

        clampf(&pivot, 1.0f, sample_rate * 0.45f);
        clampf(&tilt, -1.0f, 1.0f);

        disp_pivot = pivot;
        disp_tilt  = tilt;

        state->input_buffer[state->in_write_index] = input ? input[i] : 0.0f;
        state->in_write_index++;
        if (state->in_write_index >= FFT_SIZE)
            state->in_write_index = 0;

        state->hop_write_index++;

        if (state->hop_write_index >= HOP_SIZE) {
            state->hop_write_index = 0;

            unsigned int idx = state->in_write_index;
            for (int k = 0; k < FFT_SIZE; k++) {
                state->time_buffer[k] =
                    state->input_buffer[idx] * hann[k];
                idx++;
                if (idx >= FFT_SIZE)
                    idx = 0;
            }

            fftwf_execute(state->fft_plan);

            int bins = FFT_SIZE / 2 + 1;
            float nyquist = sample_rate * 0.5f;

            for (int k = 0; k < bins; k++) {
                float mag, phase;

                if (!freeze) {
                    mag   = hypotf(state->freq_buffer[k][0],
                                   state->freq_buffer[k][1]);
                    phase = atan2f(state->freq_buffer[k][1],
                                   state->freq_buffer[k][0]);
                    state->frozen_mag[k]   = mag;
                    state->frozen_phase[k] = phase;
                } else {
                    mag   = state->frozen_mag[k];
                    phase = state->frozen_phase[k];
                }

                float hz = ((float)k / (float)bins) * nyquist;
                if (hz < 1.0f) hz = 1.0f;

                float gain_db = tilt * 3.0f * log2f(hz / pivot);
                float gain    = powf(10.0f, gain_db / 20.0f);

                state->freq_buffer[k][0] = gain * mag * cosf(phase);
                state->freq_buffer[k][1] = gain * mag * sinf(phase);
            }

            fftwf_execute(state->ifft_plan);

            float dc = 0.0f;
            for (int k = 0; k < FFT_SIZE; k++)
                dc += state->time_buffer[k];
            dc /= FFT_SIZE;
            for (int k = 0; k < FFT_SIZE; k++)
                state->time_buffer[k] -= dc;

            for (int k = 0; k < FFT_SIZE; k++) {
                state->output_buffer[k] += state->time_buffer[k] / FFT_SIZE;
            }
        }

        out[i] = state->output_buffer[i];
    }

    memmove(state->output_buffer,
            state->output_buffer + frames,
            sizeof(float) * (FFT_SIZE - frames));
    memset(state->output_buffer + (FFT_SIZE - frames),
           0,
           sizeof(float) * frames);

    pthread_mutex_lock(&state->lock);
    state->display_pivot = disp_pivot;
    state->display_tilt  = disp_tilt;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(SpecHold* state) {
    clampf(&state->pivot_hz, 1.0f, state->sample_rate * 0.45f);
    clampf(&state->tilt, -1.0f, 1.0f);
}

static void spec_hold_draw_ui(Module* m, int y, int x) {
    SpecHold* state = (SpecHold*)m->state;

    float tilt;
	float pivot_hz;
	int freeze;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    tilt = state->display_tilt;
	pivot_hz = state->display_pivot;
	freeze = state->freeze;
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
	ORANGE(); printw(" %s | ", freeze ? "ON" : "OFF"); CLR();

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
	state->in_write_index = 0;

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

