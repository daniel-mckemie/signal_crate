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
    float attack = process_smoother(&state->smooth_attack, state->attack_ms);
    float decay  = process_smoother(&state->smooth_decay, state->decay_ms);
    float sensitivity   = process_smoother(&state->smooth_sens, state->sensitivity);
    pthread_mutex_unlock(&state->lock);

	float atk_coeff = expf(-1.0f / (0.001f * attack * state->sample_rate));
	float dec_coeff = expf(-1.0f / (0.001f * decay * state->sample_rate));

    state->display_env = state->env;

    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++) {
		float in = fabsf(m->inputs[0][i] * sensitivity);

		// Envelope (attack/decay)
		if (in > state->env)
			state->env = atk_coeff * (state->env - in) + in;
		else
			state->env = dec_coeff * (state->env - in) + in;

		// Smooth the envelope
		state->smoothed_env += 0.05f * (state->env - state->smoothed_env);

		// Apply threshold to output only
		float val = (sensitivity <= 0.0f || state->smoothed_env < state->threshold)
					? 0.0f
					: state->smoothed_env * sensitivity;

		// Downsample the control rate
		if (i % ENV_UPDATE_INTERVAL == 0) {
			for (int j = 0; j < ENV_UPDATE_INTERVAL && (i + j) < FRAMES_PER_BUFFER; j++) {
				m->control_output[i + j] = fminf(fmaxf(val, 0.0f), 1.0f); 
			}
			i += ENV_UPDATE_INTERVAL - 1;
		}
	}
}

static void c_env_fol_draw_ui(Module* m, int y, int x) {
    CEnvFol* state = (CEnvFol*)m->state;

    float atk, dec, gain, threshold, val;
    pthread_mutex_lock(&state->lock);
    atk = state->attack_ms;
	gain = state->sensitivity;
    dec = state->decay_ms;
    val = fminf(1.0f, fmaxf(0.0f, state->display_env));
	threshold = state->threshold;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[Env Follower] Env: %.3f | A: %.1fms D: %.1fms: S: %.2fx T: %.2f", val, atk, dec, gain, threshold);
    mvprintw(y+1, x, "Real-time keys: -/= (att), _/+ (dec), [/] (gain), {/} (thresh)");
    mvprintw(y+2, x, "Command mode: :1 [att], :2 [dec], :3 [gain], :4 [thresh]"); 
}

static void clamp(CEnvFol* state) {
    if (state->attack_ms < 0.1f) state->attack_ms = 0.1f;
    if (state->decay_ms < 2.0f) state->decay_ms = 2.0f;
	if (state->sensitivity < 0.1f) state->sensitivity = 0.1f;
	if (state->sensitivity > 10.0f) state->sensitivity = 10.0f;
	if (state->threshold < 0.0f) state->threshold = 0.0f;
	if (state->threshold > 1.0f) state->threshold = 1.0f;

}

static void c_env_fol_handle_input(Module* m, int key) {
    CEnvFol* s = (CEnvFol*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->attack_ms += 0.1f; handled = 1; break;
            case '-': s->attack_ms -= 0.1f; handled = 1; break;
            case '+': s->decay_ms += 0.01f; handled = 1; break;
            case '_': s->decay_ms -= 0.01f; handled = 1; break;
            case ']': s->sensitivity += 0.1f; handled = 1; break;
            case '[': s->sensitivity -= 0.1f; handled = 1; break;
            case '}': s->threshold += 0.1f; handled = 1; break;
            case '{': s->threshold -= 0.1f; handled = 1; break;
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
                if (type == '1') s->attack_ms = val;
                else if (type == '2') s->decay_ms = val;
                else if (type == '3') s->sensitivity = val;
                else if (type == '4') s->threshold = val;
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

static void c_env_fol_set_osc_param(Module* m, const char* param, float value) {
    CEnvFol* s = (CEnvFol*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "att") == 0) {
        s->attack_ms = fmaxf(1.0f, value * 1000.0f);
    } else if (strcmp(param, "dec") == 0) {
        s->decay_ms = fmaxf(1.0f, value * 1000.0f);
    } else if (strcmp(param, "sens") == 0) {
		s->sensitivity = value;
	} else if (strcmp(param, "thresh") == 0) {
		s->threshold = value;
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
    s->attack_ms = 2.0f;
    s->decay_ms = 5.0f;
	s->sensitivity = 1.0f;
	s->env = 0.0f;
	s->smoothed_env = 0.0f;
	s->threshold = 0.05;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_attack, 0.75f);
    init_smoother(&s->smooth_decay, 0.75f);
    init_smoother(&s->smooth_sens, 0.75f);
    clamp(s);

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

