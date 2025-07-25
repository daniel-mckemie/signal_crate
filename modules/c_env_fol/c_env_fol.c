#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_env_fol.h"
#include "module.h"
#include "util.h"

#define ENV_UPDATE_INTERVAL 128

static void c_env_fol_process_control(Module* m) {
	if (!m->inputs[0]) {
		endwin();
		fprintf(stderr, "[c_env_fol] Error: No audio input connected to c_env_fol. Exiting.\n");
		exit(1);
	}

    CEnvFol* state = (CEnvFol*)m->state;

    pthread_mutex_lock(&state->lock);
    float attack = process_smoother(&state->smooth_attack, state->attack_ms); // Attack not writeable (Buchla 130)
    float decay  = process_smoother(&state->smooth_decay, state->decay_ms);
    float input_gain = process_smoother(&state->smooth_gain, state->input_gain);
    float depth = process_smoother(&state->smooth_depth, state->depth);
    pthread_mutex_unlock(&state->lock);

	// Modulate with control inputs
	float mod_depth = 1.0f;
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "dec") == 0) {
			float mod_range = (5000.0f - state->decay_ms) * mod_depth;
			decay = state->decay_ms + norm * mod_range; 
        } else if (strcmp(param, "in_gain") == 0) {
			float mod_range = (1.0f - state->input_gain) * mod_depth;
            input_gain = state->input_gain + norm * mod_range;
        } else if (strcmp(param, "depth") == 0) {
			float mod_range = (1.0f - state->depth) * mod_depth;
			depth = state->depth + norm * mod_range;
        }
    }

	state->display_att = attack;
	state->display_dec = decay;
	state->display_gain = input_gain;
	state->display_depth = depth;
	state->display_env = state->smoothed_env;

	float atk_coeff = expf(-1.0f / (0.001f * attack * state->sample_rate));
	float dec_coeff = expf(-1.0f / (0.001f * decay  * state->sample_rate));


	if (!m->control_output) return;

    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++) {
		float in = fabsf(m->inputs[0][i] * input_gain);

		// Envelope (attack/decay)
		if (in > state->env)
			state->env = atk_coeff * (state->env - in) + in;
		else
			state->env = dec_coeff * (state->env - in) + in;

		// Smooth the envelope
		state->smoothed_env += 0.05f * (state->env - state->smoothed_env);

		// Apply threshold to output only
		float out = fminf(state->smoothed_env, 1.0f) * depth; // Normalized

		// Downsample the control rate
		if (i % ENV_UPDATE_INTERVAL == 0) {
			for (int j = 0; j < ENV_UPDATE_INTERVAL && (i + j) < FRAMES_PER_BUFFER; j++) {
				m->control_output[i + j] = fminf(fmaxf(out, 0.0f), 1.0f); 
			}
			i += ENV_UPDATE_INTERVAL - 1;
		}
	}
}

static void c_env_fol_draw_ui(Module* m, int y, int x) {
    CEnvFol* state = (CEnvFol*)m->state;

    float dec, gain, val, depth;
    pthread_mutex_lock(&state->lock);
    dec = state->display_dec;
	gain = state->display_gain;
	depth = state->display_depth;
    val = fminf(1.0f, fmaxf(0.0f, state->display_env));
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[EnvFol] Env: %.3f | dec: %.1fms in_gain: %.2f depth: %.2f", val, dec, gain, depth);
    mvprintw(y+1, x, "Real-time keys: -/= (dec), _/+ (in_gain), d/D (d)");
    mvprintw(y+2, x, "Command mode: :1 [dec], :2 [in_gain], :d [depth]");
}

static void clamp_params(CEnvFol* state) {
    if (state->decay_ms < 1.0f) state->decay_ms = 1.0f;
    if (state->decay_ms > 5000.0f) state->decay_ms = 5000.0f;
	if (state->input_gain < 0.1f) state->input_gain = 0.1f;
	if (state->input_gain > 1.0f) state->input_gain = 1.0f;
	if (state->depth < 0.0f) state->depth = 0.0f;
	if (state->depth > 1.0f) state->depth = 1.0f;
}

static void c_env_fol_handle_input(Module* m, int key) {
    CEnvFol* s = (CEnvFol*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->decay_ms += 0.1f; handled = 1; break;
            case '-': s->decay_ms -= 0.1f; handled = 1; break;
            case '+': s->input_gain += 0.1f; handled = 1; break;
            case '_': s->input_gain -= 0.1f; handled = 1; break;
			case 'D': s->depth += 0.1f; handled = 1; break;
            case 'd': s->depth -= 0.1f; handled = 1; break;
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
                if (type == '1') s->decay_ms = val;
                else if (type == '2') s->input_gain = val;
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

static void c_env_fol_set_osc_param(Module* m, const char* param, float value) {
    CEnvFol* s = (CEnvFol*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "dec") == 0) {
        s->decay_ms = fmaxf(1.0f, value * 5000.0f);
    } else if (strcmp(param, "in_gain") == 0) {
		s->input_gain = value;
    } else if (strcmp(param, "depth") == 0) {
		s->depth = value;
	}
    pthread_mutex_unlock(&s->lock);
}

static void c_env_fol_destroy(Module* m) {
    CEnvFol* s = (CEnvFol*)m->state;
    if (s) {
        pthread_mutex_destroy(&s->lock);
        free(s);
    }
	if (m->control_output) free(m->control_output);
}

Module* create_module(float sample_rate) {
    CEnvFol* s = calloc(1, sizeof(CEnvFol));
    s->attack_ms = 0.1f;
    s->decay_ms = 1.0f;
	s->input_gain = 0.5f;
	s->env = 0.0f;
	s->smoothed_env = 0.0f;
	s->depth = 0.5f;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_attack, 0.75f);
    init_smoother(&s->smooth_decay, 0.75f);
    init_smoother(&s->smooth_gain, 0.75f);
    init_smoother(&s->smooth_depth, 0.75f);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_env_fol";
    m->state = s;
    m->process_control = c_env_fol_process_control;
    m->draw_ui = c_env_fol_draw_ui;
    m->handle_input = c_env_fol_handle_input;
    m->set_param = c_env_fol_set_osc_param;
	m->control_output = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->destroy = c_env_fol_destroy;
    return m;
}

