#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "vco.h"
#include "module.h"
#include "util.h"

static void vco_process(Module *m, float* in, unsigned long frames) {
    VCO *state = (VCO*)m->state;
    float freq, amp;
    Waveform waveform;

	pthread_mutex_lock(&state->lock);
	float base_freq = process_smoother(&state->smooth_freq, state->frequency);
	amp = process_smoother(&state->smooth_amp, state->amplitude);
	waveform = state->waveform;
	pthread_mutex_unlock(&state->lock);
	
	freq = base_freq;  // default fallback
	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);

		if (strcmp(param, "freq") == 0) {
			float min_hz = 20.0f;
			float max_hz = 20000.0f;
			freq = min_hz * powf(max_hz / min_hz, control);
		} else if (strcmp(param, "amp") == 0) {
			amp *= control;
		}
	}
	
	state->current_freq_display = freq;
	state->current_amp_display = amp;

    float phs = state->phase;
	int idx;
    for (unsigned long i = 0; i < frames; i++) {
        float value = 0.0f;
		idx = (int)(phs / TWO_PI * SINE_TABLE_SIZE) % SINE_TABLE_SIZE;
        switch (waveform) {
            case WAVE_SINE:     value = sine_table[idx]; break;
            case WAVE_SAW: {
							   float t = phs / TWO_PI;
							   value = 2.0f * t - 1.0f;
							   value -= poly_blep(t, freq / state->sample_rate); break;		   
						   }
            case WAVE_SQUARE: {
								  float t = phs / TWO_PI;
								  value = (t < 0.5f) ? 1.0f : -1.0f;
								  float dt = freq / state->sample_rate;
								  value += poly_blep(t,dt); // Falling edge
								  value -= poly_blep(fmodf(t + 0.5f, 1.0f), dt); break; // Rising edge
							  }
            case WAVE_TRIANGLE: {
									float t = phs / TWO_PI;
									float dt = freq / state->sample_rate;
									float sq = (t < 0.5f) ? 1.0f : -1.0f;
									sq += poly_blep(t, dt);
									sq -= poly_blep(fmodf(t + 0.5f, 1.0f), dt);
									state->tri_state += 2.0f * freq / state->sample_rate * sq;
									state->tri_state *= 0.999f;
									if (state->tri_state > 1.0f) state->tri_state = 1.0f;
									if (state->tri_state < -1.0f) state->tri_state = -1.0f;
									value = state->tri_state;
									break;
								}

        }
        m->output_buffer[i] = amp * value;
		phs += TWO_PI * freq / state->sample_rate;
		if (phs >= TWO_PI) phs -= TWO_PI;
		state->phase = phs;  // store back in state
    }
}

static void vco_draw_ui(Module *m, int y, int x) {
    VCO *state = (VCO*)m->state;
    const char *wave_names[] = {"Sine", "Saw", "Square", "Triangle"};

    float freq, amp;
    Waveform waveform;

    pthread_mutex_lock(&state->lock);
    // freq = state->frequency;
    freq = state->current_freq_display;
    amp = state->current_amp_display;
    waveform = state->waveform;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y, x,   "[VCO] Freq: %.2f Hz, Amp: %.2f, Wave: %s", freq, amp, wave_names[waveform]);
    mvprintw(y+1, x, "Real-time keys: -/= (freq), _/+ (amp)");
    mvprintw(y+2, x, "Command mode: :1 [freq], :2 [amp], :w [waveform]");
}

static void clamp_params(VCO *state) {
    if (state->frequency < 0.01f) state->frequency = 0.01f;
    if (state->frequency > 20000.0f) state->frequency = 20000.0f;
    if (state->amplitude < 0.0f) state->amplitude = 0.0f;
    if (state->amplitude > 1.0f) state->amplitude = 1.0f;
}

static void vco_handle_input(Module *m, int key) {
    VCO *state = (VCO*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->frequency += 0.5f; handled = 1; break;
            case '-': state->frequency -= 0.5f; handled = 1; break;
            case '+': state->amplitude += 0.01f; handled = 1; break;
            case '_': state->amplitude -= 0.01f; handled = 1; break;
            case 'w': state->waveform = (state->waveform + 1) % 4; handled = 1; break;
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
                if (type == '1') state->frequency = val;
                else if (type == '2') state->amplitude = val;
                else if (type == '3') state->waveform = ((int)val) % 4;
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


static void vco_set_osc_param(Module* m, const char* param, float value) {
    VCO* state = (VCO*)m->state;
    pthread_mutex_lock(&state->lock);

	// The "param" name should always match the UI for easy programming
	// The state param name may differ, as with freq_mod
    if (strcmp(param, "freq") == 0) {
		// Expect 0.0–1.0 from slider, map to 20 Hz – 20000 Hz
		float min_hz = 20.0f;
		float max_hz = 20000.0f;
		float norm = fminf(fmaxf(value, 0.0f), 1.0f); // clamp 0–1
		float hz = min_hz * powf(max_hz / min_hz, norm);  // exponential mapping
        state->frequency = hz;
    } else if (strcmp(param, "amp") == 0) {
        state->amplitude = value;
    } else if (strcmp(param, "wave") == 0) {
		if (value > 0.5f) {
			state->waveform = (Waveform)((state->waveform + 1) % 4);
		}
    } else {
        fprintf(stderr, "[vco] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&state->lock);
}


static void vco_destroy(Module* m) {
    if (!m) return;
    VCO* state = (VCO*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
    VCO *state = calloc(1, sizeof(VCO));
    state->frequency = 440.0f;
    state->amplitude = 0.5f;
    state->waveform = WAVE_SINE;
	state->phase = 0.0f;
	state->tri_state = 0.0f;
    state->sample_rate = sample_rate;
    
	pthread_mutex_init(&state->lock, NULL);
	init_sine_table();
    init_smoother(&state->smooth_freq, 0.75f);
    init_smoother(&state->smooth_amp, 0.75f);
    clamp_params(state);

    Module *m = calloc(1, sizeof(Module));
    m->name = "vco";
    m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = vco_process;
    m->draw_ui = vco_draw_ui;
	m->handle_input = vco_handle_input;
	m->set_param = vco_set_osc_param;
	m->destroy = vco_destroy;
	m->control_output = NULL;
    return m;
}

