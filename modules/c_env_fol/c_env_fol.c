#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_env_fol.h"
#include "module.h"
#include "util.h"

#define ENV_UPDATE_INTERVAL 16

static void c_env_fol_process_control(Module* m, unsigned long frames) {
    if (!m->inputs[0]) {
        endwin();
        fprintf(stderr, "[c_env_fol] Error: No audio input connected.\n");
        exit(1);
    }

    CEnvFol* s = (CEnvFol*)m->state;

    pthread_mutex_lock(&s->lock);
    float base_attack = s->attack_ms;    // not user-writeable
    float base_decay  = s->decay_ms;
    float base_sens   = s->sens;
    float base_depth  = s->depth;
    pthread_mutex_unlock(&s->lock);

    float mod_decay  = base_decay;
    float mod_sens   = base_sens;
    float mod_depth  = base_depth;

    float mod_depth_amount = 1.0f;

    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
        float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "dec") == 0) {
            float mod_range = (5000.0f - base_decay) * mod_depth_amount;
            mod_decay = base_decay + norm * mod_range;

        } else if (strcmp(param, "sens") == 0) {
            float mod_range = (1.0f - base_sens) * mod_depth_amount;
            mod_sens = base_sens + norm * mod_range;

        } else if (strcmp(param, "depth") == 0) {
            float mod_range = (1.0f - base_depth) * mod_depth_amount;
            mod_depth = base_depth + norm * mod_range;
        }
    }

    mod_decay = fminf(fmaxf(mod_decay, 1.0f), 5000.0f);
    mod_sens  = fminf(fmaxf(mod_sens, 0.01f), 1.0f);
    mod_depth = fminf(fmaxf(mod_depth, 0.0f), 1.0f);

    float smooth_att   = process_smoother(&s->smooth_attack, base_attack);
    float smooth_decay = process_smoother(&s->smooth_decay, mod_decay);
    float smooth_sens  = process_smoother(&s->smooth_gain,  mod_sens);
    float smooth_depth = process_smoother(&s->smooth_depth, mod_depth);

    pthread_mutex_lock(&s->lock);
    s->display_att   = smooth_att;
    s->display_dec   = smooth_decay;
    s->display_gain  = smooth_sens;
    s->display_depth = smooth_depth;
    s->display_env   = s->smoothed_env;
    pthread_mutex_unlock(&s->lock);

    float atk_coeff = expf(-1.0f / (0.001f * smooth_att   * s->sample_rate));
    float dec_coeff = expf(-1.0f / (0.001f * smooth_decay * s->sample_rate));

    for (unsigned long i = 0; i < frames; i++) {
        float in = fabsf(m->inputs[0][i] * smooth_sens);

        if (in > s->env)
            s->env = atk_coeff * (s->env - in) + in;
        else
            s->env = dec_coeff * (s->env - in) + in;

        // smoothing filter
        s->smoothed_env += 0.05f * (s->env - s->smoothed_env);

        // final CV out
        float out = fminf(s->smoothed_env, 1.0f) * smooth_depth;

        m->control_output[i] = out;
    }
}

static void clamp_params(CEnvFol* state) {
    clampf(&state->decay_ms, 1.0f, 5000.0f);
    clampf(&state->sens,     0.01f, 1.0f);
    clampf(&state->depth,    0.0f, 1.0f);
}

static void c_env_fol_draw_ui(Module* m, int y, int x) {
    CEnvFol* state = (CEnvFol*)m->state;

    float dec, sensitivity, val, depth;
    pthread_mutex_lock(&state->lock);
    dec = state->display_dec;
	sensitivity = state->display_gain;
	depth = state->display_depth;
    val = fminf(1.0f, fmaxf(0.0f, state->display_env));
    pthread_mutex_unlock(&state->lock);

	BLUE();
    mvprintw(y,   x, "[EnvFol:%s] ", m->name);
	CLR();

	LABEL(2,"Env:");
	ORANGE(); printw(" %.3f | ", val); CLR();

	LABEL(2,"dec:");
	ORANGE(); printw(" %.1fms | ", dec); CLR();

	LABEL(2,"sens:");
	ORANGE(); printw(" %.2f | ", sensitivity); CLR();

	LABEL(2,"depth:");
	ORANGE(); printw(" %.2f", depth); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= (dec), _/+ (sens), d/D (d)");
    mvprintw(y+2, x, "Command mode: :1 [dec], :2 [sens], :d [depth]");
	BLACK();
}

static void c_env_fol_handle_input(Module* m, int key) {
    CEnvFol* s = (CEnvFol*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->decay_ms += 0.1f; handled = 1; break;
            case '-': s->decay_ms -= 0.1f; handled = 1; break;
            case '+': s->sens += 0.01f; handled = 1; break;
            case '_': s->sens -= 0.01f; handled = 1; break;
			case 'D': s->depth += 0.01f; handled = 1; break;
            case 'd': s->depth -= 0.01f; handled = 1; break;
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
                else if (type == '2') s->sens = val;
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
    } else if (strcmp(param, "sens") == 0) {
		s->sens = value;
    } else if (strcmp(param, "depth") == 0) {
		s->depth = value;
	}
    pthread_mutex_unlock(&s->lock);
}

static void c_env_fol_destroy(Module* m) {
    CEnvFol* state = (CEnvFol*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float decay_ms = 1.0f;
	float sens = 0.5f;
	float depth = 0.5f;

    if (args && strstr(args, "dec=")) {
        sscanf(strstr(args, "dec="), "dec=%f", &decay_ms);
	}
	if (args && strstr(args, "sens=")) {
        sscanf(strstr(args, "sens="), "sens=%f", &sens);
    }
	if (args && strstr(args, "depth=")) {
        sscanf(strstr(args, "depth="), "depth=%f", &depth);
    }

    CEnvFol* s = calloc(1, sizeof(CEnvFol));
    s->attack_ms = 0.1f;
    s->decay_ms = decay_ms; 
	s->sens = sens; 
	s->env = 0.0f;
	s->smoothed_env = 0.0f;
	s->depth = depth;
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
	m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->destroy = c_env_fol_destroy;
    return m;
}

