#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_lfo.h"
#include "module.h"
#include "util.h"


static void c_lfo_process_control(Module* m) {
    CLFO* state = (CLFO*)m->state;

    pthread_mutex_lock(&state->lock);
    float base_freq = process_smoother(&state->smooth_freq, state->frequency);
    float amp = process_smoother(&state->smooth_amp, state->amplitude);
    float depth = process_smoother(&state->smooth_depth, state->depth);
    LFOWaveform wf = state->waveform;
    pthread_mutex_unlock(&state->lock);

	float freq = base_freq;
	float mod_depth = 0.5f;

    // Modulate with control inputs
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
		float norm = fminf(fmaxf(control, 0.0f), 1.0f);

        if (strcmp(param, "freq") == 0) {
			float mod_range = state->frequency * mod_depth;
            freq = state->frequency + norm * mod_range; 
        } else if (strcmp(param, "amp") == 0) {
			float mod_range = state->amplitude * mod_depth;
			amp = state->amplitude + (2.0f * norm - 1.0f) * mod_range;
        } else if (strcmp(param, "depth") == 0) {
			float mod_range = state->depth * mod_depth;
			depth = state->depth * (2.0f * norm - 1.0f) * mod_range;
        }
    }

	m->control_output_depth = fminf(fmaxf(mod_depth, 0.0f), 1.0f);

    if (!m->control_output) return;

    state->display_freq = freq;
    state->display_amp = amp;
	state->display_depth = depth;

    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++) {
        float t = state->phase / TWO_PI;
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
                state->tri_state += 2.0f * freq / state->sample_rate * sq;
                state->tri_state *= 0.999f;
                if (state->tri_state > 1.0f) state->tri_state = 1.0f;
                if (state->tri_state < -1.0f) state->tri_state = -1.0f;
                value = state->tri_state;
                break;
            }
        }
	
		float out = depth * (amp * (0.5f + 0.5f * value));
		m->control_output[i] = fminf(fmaxf(out, 0.0f), 1.0f);

        state->phase += TWO_PI * freq / state->sample_rate;
        if (state->phase >= TWO_PI) state->phase -= TWO_PI;
    }
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
    wf = state->waveform;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[LFO] Freq: %.2f Hz, Amp: %.2f, Depth: %.2f, Wave: %s", freq, amp, depth, names[wf]);
    mvprintw(y+1, x, "Real-time keys: -/= (freq), _/+ (amp), d/D (depth)");
    mvprintw(y+2, x, "Command mode: :1 [freq], :2 [amp], :d [depth], :w [waveform]");
}

static void clamp(CLFO* s) {
    if (s->frequency < 0.01f) s->frequency = 0.01f;
    if (s->frequency > 100.0f) s->frequency = 100.0f;
    if (s->amplitude < 0.0f) s->amplitude = 0.0f;
    if (s->amplitude > 1.0f) s->amplitude = 1.0f;
	if (s->depth < 0.0f) s->depth = 0.0f;
	if (s->depth > 1.0f) s->depth = 1.0f;
}

static void c_lfo_handle_input(Module* m, int key) {
    CLFO* s = (CLFO*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->frequency += 0.1f; handled = 1; break;
            case '-': s->frequency -= 0.1f; handled = 1; break;
            case '+': s->amplitude += 0.01f; handled = 1; break;
            case '_': s->amplitude -= 0.01f; handled = 1; break;
			case 'd': s->depth += 0.01f; handled = 1; break;
			case 'D': s->depth -= 0.01f; handled = 1; break;
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

    if (handled) clamp(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_lfo_set_osc_param(Module* m, const char* param, float value) {
    CLFO* s = (CLFO*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "freq") == 0) {
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        s->frequency = 0.1f * powf(100.0f / 0.1f, norm);  // exponential map
    } else if (strcmp(param, "amp") == 0) {
        s->amplitude = value;
    } else if (strcmp(param, "depth") == 0) {
		s->depth = value;
    } else if (strcmp(param, "wave") == 0) {
        if (value > 0.5f) s->waveform = (s->waveform + 1) % 4;
    }

    pthread_mutex_unlock(&s->lock);
}

static void c_lfo_destroy(Module* m) {
    CLFO* s = (CLFO*)m->state;
    if (s) {
        pthread_mutex_destroy(&s->lock);
        free(s);
    }
	if (m->control_output) free(m->control_output);
}

Module* create_module(float sample_rate) {
    CLFO* s = calloc(1, sizeof(CLFO));
    s->frequency = 1.0f;
    s->amplitude = 1.0f;
    s->waveform = LFO_SINE;
    s->phase = 0.0f;
    s->tri_state = 0.0f;
	s->depth = 0.5f;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_freq, 0.75f);
    init_smoother(&s->smooth_amp, 0.75f);
    init_smoother(&s->smooth_depth, 0.75f);
    init_sine_table();
    clamp(s);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_lfo";
    m->state = s;
    m->process_control = c_lfo_process_control;
    m->draw_ui = c_lfo_draw_ui;
    m->handle_input = c_lfo_handle_input;
    m->set_param = c_lfo_set_osc_param;
	m->control_output = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->destroy = c_lfo_destroy;
    return m;
}

