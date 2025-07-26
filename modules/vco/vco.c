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
	freq = process_smoother(&state->smooth_freq, state->frequency);
	amp = state->amplitude;
	waveform = state->waveform;
	pthread_mutex_unlock(&state->lock);
	
	float mod_depth = 1.0f;
	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

		if (strcmp(param, "freq") == 0) {
			float mod_range = state->frequency * mod_depth;
			freq = state->frequency + norm * mod_range;

		} else if (strcmp(param, "amp") == 0) {
			float mod_range = (1.0f - state->amplitude) * mod_depth;
			amp = state->amplitude + norm * mod_range;
		}
	}
	
	state->display_freq = freq;
	state->display_amp = amp;

    float phs = state->phase;
	int idx;
    for (unsigned long i = 0; i < frames; i++) {
        float value = 0.0f;
		float smoothed_amp = process_smoother(&state->smooth_amp, amp);
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
									// value = tanhf(state->tri_state) * 2.0f; // replace if getting aliasing at high freq, performance intensive
									value = state->tri_state * 2.0f;
									break;
								}

        }
        m->output_buffer[i] = smoothed_amp * value;
		phs += TWO_PI * freq / state->sample_rate;
		if (phs >= TWO_PI) phs -= TWO_PI;
		state->phase = phs;  // store back in state
    }
}

static void vco_draw_ui(Module *m, int y, int x) {
    VCO *state = (VCO*)m->state;
    const char *wave_names[] = {"Sine", "Saw", "Square", "Triangle"};
	const char *range_names[] = {"Low", "Mid", "Full", "Super"};

    float freq, amp;
    Waveform waveform;
	RangeMode range;

    pthread_mutex_lock(&state->lock);
    freq = state->display_freq;
    amp = state->display_amp;
    waveform = state->waveform;
	range = state->range_mode;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y, x,   "[VCO:%s] Freq: %.1f Hz | Amp: %.2f | Wave: %s | Range: %s", m->name, freq, amp, wave_names[waveform], range_names[range]);
    mvprintw(y+1, x, "Real-time keys: -/= (freq), _/+ (amp), w (wave), r (range)");
    mvprintw(y+2, x, "Command mode: :1 [freq], :2 [amp]");
}

static void clamp_params(VCO *state) {
    float min_freq = 20.0f;
    float max_freq;

    switch (state->range_mode) {
        case RANGE_LOW:   max_freq = 2000.0f; break;
        case RANGE_MID:   max_freq = 8000.0f; break;
        case RANGE_FULL:  max_freq = 20000.0f; break;
		case RANGE_SUPER: max_freq = state->sample_rate * 0.45; break;
    }

    if (state->frequency < min_freq) state->frequency = min_freq;
    if (state->frequency > max_freq) state->frequency = max_freq;

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
			case 'r': state->range_mode = (state->range_mode + 1) % 4; handled = 1; break;
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
		state->display_freq = state->frequency;
		state->display_amp = state->amplitude;

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
		float max_hz;
		switch (state->range_mode) {
			case RANGE_LOW:   max_hz = 2000.0f; break;
			case RANGE_MID:   max_hz = 8000.0f; break;
			case RANGE_FULL:  max_hz = 20000.0f; break;
			case RANGE_SUPER: max_hz = state->sample_rate * 0.45f; break;
			default:          max_hz = 20000.0f; break;
		}
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
    VCO* state = (VCO*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float freq = 440.0f;
    float amp = 0.5f;
	Waveform wave = WAVE_SINE;

	if (args && strstr(args, "freq=")) {
        sscanf(strstr(args, "freq="), "freq=%f", &freq);
    }
    if (args && strstr(args, "amp=")) {
        sscanf(strstr(args, "amp="), "amp=%f", &amp);
	}
	if (args && strstr(args, "wave=")) {
        char wave_str[32] = {0};
        sscanf(strstr(args, "wave="), "wave=%31s", wave_str);

        if (strcmp(wave_str, "sine") == 0) wave = WAVE_SINE;
        else if (strcmp(wave_str, "saw") == 0) wave = WAVE_SAW;
        else if (strcmp(wave_str, "square") == 0) wave = WAVE_SQUARE;
        else if (strcmp(wave_str, "triangle") == 0) wave = WAVE_TRIANGLE;
        else fprintf(stderr, "[vco] Unknown wave type: '%s'\n", wave_str);
    }

    VCO *state = calloc(1, sizeof(VCO));
    state->frequency = freq;
    state->amplitude = amp;
    state->waveform = wave;
	state->range_mode = RANGE_LOW;
	state->phase = 0.0f;
	state->tri_state = 0.0f;
    state->sample_rate = sample_rate;
    
	pthread_mutex_init(&state->lock, NULL);
	init_sine_table();
    init_smoother(&state->smooth_freq, 0.75f);
    init_smoother(&state->smooth_amp, 0.95f);
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

