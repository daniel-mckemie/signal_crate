#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <ncurses.h>
#include <fftw3.h>

#include "spec_tilt.h"
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

static void clamp_params(SpecTilt* state) {
	if (state->tilt < -1.0f) state->tilt = -1.0f;
	if (state->tilt >  1.0f) state->tilt = 1.0f;
	
	if (state->pivot_hz < 1.0f) state->pivot_hz = 1.0f;
	if (state->pivot_hz > 20000.0f) state->pivot_hz = 20000.0f;
}

static void spec_tilt_process(Module* m, float* in, unsigned long frames) {
	SpecTilt* state = (SpecTilt*)m->state;

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

			pthread_mutex_lock(&state->lock);
			float tilt = process_smoother(&state->smooth_tilt, state->tilt);
			float pivot_hz = process_smoother(&state->smooth_pivot_hz, state->pivot_hz);
			pthread_mutex_unlock(&state->lock);

			for (int j = 0; j < bins; j++) {
				if (fabsf(tilt) < 1e-4f) continue;  // skip gain adjustment when flat
				float bin_hz = ((float)j / (float)bins) * nyquist;
				bin_hz = fmaxf(bin_hz, 1.0f);  // avoid log(0) or near-zero
				float gain_db = tilt * 3.0f * log2f(bin_hz / pivot_hz);
				float gain = powf(10.0f, gain_db / 20.0f);

				float mag = hypotf(state->freq_buffer[j][0], state->freq_buffer[j][1]);
				float phase = atan2f(state->freq_buffer[j][1], state->freq_buffer[j][0]);

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

static void spec_tilt_draw_ui(Module* m, int y, int x) {
    SpecTilt* state = (SpecTilt*)m->state;

    float tilt;
	float pivot_hz;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    tilt = state->tilt;
	pivot_hz = state->pivot_hz;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[SpecTilt] Tilt: %.2f", tilt);
    mvprintw(y+1, x, "	   Pivot (Hz): %.2f", pivot_hz);
    mvprintw(y+2, x, "Keys: - / = to tilt low/high");
    mvprintw(y+3, x, "Keys: _ / + to pivot_hz");
    mvprintw(y+4, x, "Cmd: :1 [tilt], :2 [pivot_hz]");
    if (state->entering_command)
        mvprintw(y+3, x, "%s", cmd);
}

static void spec_tilt_handle_input(Module* m, int key) {
    SpecTilt* state = (SpecTilt*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->tilt += 0.01f; handled = 1; break;
            case '-': state->tilt -= 0.01f; handled = 1; break;
            case '+': state->pivot_hz += 1.0f; handled = 1; break;
            case '_': state->pivot_hz -= 1.0f; handled = 1; break;
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
                if (type == '1') {state->tilt = val;}
				else if (type == '2') {state->pivot_hz = val;}
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

static void spec_tilt_destroy(Module* m) {
    if (!m) return;
    SpecTilt* state = (SpecTilt*)m->state;
    if (state) {
        fftwf_destroy_plan(state->fft_plan);
        fftwf_destroy_plan(state->ifft_plan);
        fftwf_free(state->time_buffer);
        fftwf_free(state->freq_buffer);
        free(state->input_buffer);
        free(state->output_buffer);
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
    SpecTilt* state = calloc(1, sizeof(SpecTilt));
    state->sample_rate = sample_rate;
    state->tilt = 0.0f;
	state->pivot_hz = 1000.0f;
    pthread_mutex_init(&state->lock, NULL);

    init_smoother(&state->smooth_tilt, 0.75f);
    init_smoother(&state->smooth_pivot_hz, 0.75f);
    clamp_params(state);

    init_hann_window();

    state->input_buffer = calloc(FFT_SIZE, sizeof(float));
    state->output_buffer = calloc(FFT_SIZE, sizeof(float));
    state->time_buffer = fftwf_alloc_real(FFT_SIZE);
    state->freq_buffer = fftwf_alloc_complex(FFT_SIZE / 2 + 1);
	memset(state->output_buffer, 0, sizeof(float) * FFT_SIZE);

    state->fft_plan = fftwf_plan_dft_r2c_1d(FFT_SIZE, state->time_buffer, state->freq_buffer, FFTW_ESTIMATE);
    state->ifft_plan = fftwf_plan_dft_c2r_1d(FFT_SIZE, state->freq_buffer, state->time_buffer, FFTW_ESTIMATE);

	state->hop_write_index = 0;

    Module* m = calloc(1, sizeof(Module));
    m->name = "spec_tilt";
    m->state = state;
    m->output_buffer = calloc(HOP_SIZE, sizeof(float));
    m->process = spec_tilt_process;
    m->draw_ui = spec_tilt_draw_ui;
    m->handle_input = spec_tilt_handle_input;
    m->destroy = spec_tilt_destroy;
    return m;
}

