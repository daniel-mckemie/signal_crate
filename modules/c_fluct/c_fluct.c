#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_fluct.h"
#include "module.h"
#include "util.h"

static void clamp_params(CFluct* s) {
    clampf(&s->rate, 0.001f, 20.0f);
    clampf(&s->depth, 0.0f, 1.0f);
}

static void c_fluct_process_control(Module* m) {
    CFluct* s = (CFluct*)m->state;

    pthread_mutex_lock(&s->lock);
    float rate = process_smoother(&s->smooth_rate, s->rate);
    float depth = process_smoother(&s->smooth_depth, s->depth);
    pthread_mutex_unlock(&s->lock);

    float dt = 1.0f / s->sample_rate;
	float mod_depth = 1.0f;
	for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "rate") == 0) {
			float mod_range = (20.0f - s->rate) * mod_depth;
			rate = s->rate + norm * mod_range;
        } else if (strcmp(param, "depth") == 0) {
			float mod_range = (1.0f - s->depth) * mod_depth;
			depth = s->depth + norm * mod_range;
        }
    }

	m->control_output_depth = fminf(fmaxf(mod_depth, 0.0f), 1.0f);

    if (!m->control_output) return;

    s->display_rate = rate;
	s->display_depth = depth;

    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++) {
        s->phase += dt;

        if (s->phase >= 1.0f / rate) {
            s->phase -= 1.0f / rate;

            if (s->mode == FLUCT_NOISE) {
				s->prev_value = s->current_value;
				s->target_value = randf() * 2.0f - 1.0f;
            } else if (s->mode == FLUCT_WALK) {
                s->prev_value = s->current_value;
                s->target_value = s->prev_value + (randf() * 2.0f - 1.0f) * 0.1f;
                s->target_value = fminf(fmaxf(s->target_value, -1.0f), 1.0f);
            }
        }

		float slew = dt * rate;
        s->current_value += (s->target_value - s->current_value) * fminf(slew, 1.0f);

        float out = depth * s->current_value;
        m->control_output[i] = out;
    }
}

static void c_fluct_draw_ui(Module* m, int y, int x) {
    CFluct* s = (CFluct*)m->state;
    const char* modes[] = {"Noise", "Walk"};
    pthread_mutex_lock(&s->lock);
    float rate = s->display_rate;
    float depth = s->display_depth;
    FluctMode mode = s->mode;
    pthread_mutex_unlock(&s->lock);

    mvprintw(y,   x, "[Fluct:%s] Rate: %.3f Hz | Depth: %.2f | Mode: %s", m->name, rate, depth, modes[mode]);
    mvprintw(y+1, x, "Keys: -/= (rate), d/D (depth), m (mode)");
    mvprintw(y+2, x, "Cmd: :1 [rate], :d [depth]");
}

static void c_fluct_handle_input(Module* m, int key) {
    CFluct* s = (CFluct*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);
    if (!s->entering_command) {
        switch (key) {
            case '=': s->rate += 0.01f; handled = 1; break;
            case '-': s->rate -= 0.01f; handled = 1; break;
            case 'D': s->depth += 0.01f; handled = 1; break;
            case 'd': s->depth -= 0.01f; handled = 1; break;
            case 'm': s->mode = (s->mode + 1) % 2; handled = 1; break;
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
                if (type == '1') s->rate = val;
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

static void c_fluct_set_osc_param(Module* m, const char* param, float value) {
    CFluct* s = (CFluct*)m->state;
    pthread_mutex_lock(&s->lock);
    if (strcmp(param, "rate") == 0)
        s->rate = 0.01f * powf(20.0f / 0.01f, value);
    else if (strcmp(param, "depth") == 0)
        s->depth = value;
    else if (strcmp(param, "mode") == 0)
        s->mode = (value > 0.5f) ? FLUCT_WALK : FLUCT_NOISE;
    pthread_mutex_unlock(&s->lock);
}

static void c_fluct_destroy(Module* m) {
    CFluct* s = (CFluct*)m->state;
    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float rate = 1.00f;
    float depth = 0.5f;
	FluctMode mode = FLUCT_WALK;

	if (args && strstr(args, "rate=")) {
        sscanf(strstr(args, "rate="), "rate=%f", &rate);
	}
	if (args && strstr(args, "depth=")) {
        sscanf(strstr(args, "depth="), "depth=%f", &depth);
    }
	if (args && strstr(args, "mode=")) {
        char mode_str[32] = {0};
        sscanf(strstr(args, "mode="), "mode=%31s", mode_str);
        if (strcmp(mode_str, "noise") == 0) mode = FLUCT_NOISE;
        else if (strcmp(mode_str, "walk") == 0) mode = FLUCT_WALK;
        else fprintf(stderr, "[vco] Unknown wave type: '%s'\n", mode_str);
    }


    CFluct* s = calloc(1, sizeof(CFluct));
    s->rate = rate;
    s->depth = depth;
    s->phase = 0.0f;
    s->mode = mode;
    s->sample_rate = sample_rate;
    s->current_value = 0.0f;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_rate, 0.75f);
    init_smoother(&s->smooth_depth, 0.75f);
    clamp_params(s);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_fluct";
    m->state = s;
    m->process_control = c_fluct_process_control;
    m->draw_ui = c_fluct_draw_ui;
    m->handle_input = c_fluct_handle_input;
    m->set_param = c_fluct_set_osc_param;
    m->destroy = c_fluct_destroy;
    m->control_output = calloc(FRAMES_PER_BUFFER, sizeof(float));
    return m;
}

