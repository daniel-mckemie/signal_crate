#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_lfo.h"
#include "module.h"
#include "util.h"

static void c_lfo_process_control(Module* m, unsigned long frames) {
    CLFO* s = (CLFO*)m->state;
	float* out = m->control_output;

    float base_freq, base_amp, base_depth;
    LFOWaveform wf;

    pthread_mutex_lock(&s->lock);
    base_freq  = s->frequency;
    base_amp   = s->amplitude;
    base_depth = s->depth;
    wf         = s->waveform;
    pthread_mutex_unlock(&s->lock);

    float freq_s  = process_smoother(&s->smooth_freq,  base_freq);
    float amp_s   = process_smoother(&s->smooth_amp,   base_amp);
    float depth_s = process_smoother(&s->smooth_depth, base_depth);

	float disp_freq  = freq_s;
	float disp_amp   = amp_s;
	float disp_depth = depth_s;

	for (unsigned long i=0; i<frames; i++) {
		float freq = freq_s;
		float amp  = amp_s;
		float depth = depth_s;

		for (int j=0; j<m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "freq") == 0) {
				freq += control * freq_s;
			} else if (strcmp(param, "amp") == 0) {
				amp += control;
			} else if (strcmp(param, "depth") == 0) {
				depth += control;
			}
		}

		clampf(&freq, 0.001f, 100.0f);
		clampf(&amp, 0.0f, 1.0f);
		clampf(&depth, 0.0f, 1.0f);

		float sr = s->sample_rate;
		const float* sine_table = get_sine_table();

		float t = s->phase / TWO_PI;
		float value = 0.0f;

		switch (wf) {
			case LFO_SINE:
				value = sine_table[(int)(t * SINE_TABLE_SIZE) % SINE_TABLE_SIZE];
				break;
			case LFO_SAW:
				value = 2.0f * t - 1.0f;
				break;
			case LFO_SQUARE:
				value = (t < 0.5f) ? 1.0f : -1.0f;
				break;
			case LFO_TRIANGLE: {
				float sq = (t < 0.5f) ? 1.0f : -1.0f;
				s->tri_state += 2.0f * freq / sr * sq;
				value = tanhf(s->tri_state);
				break;
			}
		}

		disp_freq  = freq;
		disp_amp   = amp;
		disp_depth = depth;

		if (s->polarity) {
			value = depth * amp * value;
		} else {
			value = depth * (0.5f + 0.5f * amp * value);
		}
		out[i] = value;

		s->phase += TWO_PI * freq / sr;
		if (s->phase >= TWO_PI) s->phase -= TWO_PI;
	}
	pthread_mutex_lock(&s->lock);
	s->display_freq  = disp_freq;
	s->display_amp   = disp_amp;
	s->display_depth = disp_depth;
	s->display_wave = wf;
	pthread_mutex_unlock(&s->lock);
}

static void clamp_params(CLFO* s) {
    clampf(&s->frequency, 0.001f, 100.0f);
    clampf(&s->amplitude, 0.0f, 1.0f);
    clampf(&s->depth,     0.0f, 1.0f);
}


static void c_lfo_draw_ui(Module* m, int y, int x) {
    CLFO* state = (CLFO*)m->state;
    const char* names[] = {"Sine", "Saw", "Square", "Triangle"};

    float freq, amp, depth;
    LFOWaveform wf;

    pthread_mutex_lock(&state->lock);
    freq = state->display_freq;
    amp = state->display_amp;
	depth = state->display_depth;
    wf = state->display_wave;
    pthread_mutex_unlock(&state->lock);

	BLUE();
    mvprintw(y,   x, "[LFO:%s] ", m->name);
	CLR();

	LABEL(2, "freq:");
	ORANGE(); printw(" %.2f Hz|", freq); CLR();

	LABEL(2, "amp:");
	ORANGE(); printw(" %.2f|", amp); CLR();

	LABEL(2, "depth:");
	ORANGE(); printw(" %.2f|", depth); CLR();

	LABEL(2, "wave:");
	ORANGE(); printw("%s|", names[wf]); CLR();
	
	LABEL(2, "pole:");
	ORANGE(); printw(" %s", state->polarity ? "bi" : "uni"); CLR();

	YELLOW();
    mvprintw(y+1, x, "Keys: -/= (freq), _/+ (amp), d/D (depth), w (wave), p (polarity)");
    mvprintw(y+2, x, "Command mode: :1 [freq], :2 [amp], :d [depth]");
	BLACK();
}

static void c_lfo_handle_input(Module* m, int key) {
    CLFO* s = (CLFO*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->frequency += 0.01f; handled = 1; break;
            case '-': s->frequency -= 0.01f; handled = 1; break;
            case '+': s->amplitude += 0.01f; handled = 1; break;
            case '_': s->amplitude -= 0.01f; handled = 1; break;
			case 'D': s->depth += 0.01f; handled = 1; break;
			case 'd': s->depth -= 0.01f; handled = 1; break;
			case 'p': s->polarity = !s->polarity; handled = 1; break;
            case 'w': s->waveform = (s->waveform + 1) % 4; handled = 1; break;
            case ':':
                s->entering_command = true;
                memset(s->command_buffer, 0, sizeof(s->command_buffer));
                s->command_index = 0;
                handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            s->entering_command = false;
            char type;
            float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') s->frequency = val;
                else if (type == '2') s->amplitude = val;
                else if (type == '3') s->waveform = ((int)val) % 4;
                else if (type == 'd') s->depth = val; 
            }
            handled = 1;
        } else if (key == 27) {
            s->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 && s->command_index < sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled) clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_lfo_set_osc_param(Module* m, const char* param, float value) {
    CLFO* s = (CLFO*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "freq") == 0) {
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        s->frequency = 0.1f * powf(100.0f / 0.1f, norm);  // exponential map
    } else if (strcmp(param, "amp") == 0) {
        s->amplitude = fminf(fmaxf(value, 0.0f), 1.0f);
    } else if (strcmp(param, "depth") == 0) {
		s->depth = fminf(fmaxf(value, 0.0f), 1.0f);
    } else if (strcmp(param, "wave") == 0) {
        if (value > 0.5f) s->waveform = (s->waveform + 1) % 4;
    } else if (strcmp(param, "polarity") == 0) {
		s->polarity = (value > 0.5f);
	}
	clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_lfo_destroy(Module* m) {
    CLFO* state = (CLFO*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float frequency = 1.0f;
	float amplitude = 1.0f;
	float depth = 0.5f;
    if (args && strstr(args, "freq=")) {
        sscanf(strstr(args, "freq="), "freq=%f", &frequency);
	}
	if (args && strstr(args, "amp=")) {
        sscanf(strstr(args, "amp="), "amp=%f", &amplitude);
    }
	if (args && strstr(args, "depth=")) {
        sscanf(strstr(args, "depth="), "depth=%f", &depth);
    }
	
    CLFO* s = calloc(1, sizeof(CLFO));
    s->frequency = frequency;
    s->amplitude = amplitude;
    s->waveform = LFO_SINE;
    s->phase = 0.0f;
    s->tri_state = 0.0f;
	s->polarity = 0;
	s->depth = depth;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_freq, 0.75f);
    init_smoother(&s->smooth_amp, 0.75f);
    init_smoother(&s->smooth_depth, 0.75f);
    init_sine_table();
    clamp_params(s);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_lfo";
    m->state = s;
    m->process_control = c_lfo_process_control;
    m->draw_ui = c_lfo_draw_ui;
    m->handle_input = c_lfo_handle_input;
    m->set_param = c_lfo_set_osc_param;
	m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->destroy = c_lfo_destroy;
    return m;
}

