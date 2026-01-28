#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "mixer.h"
#include "module.h"
#include "util.h"

static inline void clamp_params(MixerState* s) {
    clampf(&s->gain, 0.0f, 8.0f);
}

static void mixer_process(Module* m, float* in, unsigned long frames) {
    (void)in;
    MixerState* s = (MixerState*)m->state;
    float* out = m->output_buffer;

    pthread_mutex_lock(&s->lock);
    float base_gain = s->gain;
    pthread_mutex_unlock(&s->lock);

    float gain_s = process_smoother(&s->smooth_gain, base_gain);
    float disp_gain = gain_s;

    for (unsigned long i = 0; i < frames; i++) {
        float gain = gain_s;

        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "gain") == 0) {
                gain += control;
            }
        }

        clampf(&gain, 0.0f, 8.0f);
        disp_gain = gain;

		float sum = 0.0f;
		int active = 0;

		for (int ch = 0; ch < m->num_inputs; ch++) {
			if (m->inputs[ch]) {
				sum += m->inputs[ch][i];
				active++;
			}
		}

		float norm = (active > 0) ? (0.707f / (float)active) : 0.0f;
		out[i] = fminf(fmaxf(sum * norm * gain, -1.0f), 1.0f);

    }

    pthread_mutex_lock(&s->lock);
    s->display_gain = disp_gain;
    pthread_mutex_unlock(&s->lock);
}

static void mixer_draw_ui(Module* m, int y, int x) {
    MixerState* s = (MixerState*)m->state;

    pthread_mutex_lock(&s->lock);
    float gain = s->display_gain;
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[Mixer:%s] ", m->name);
    CLR();

    LABEL(2, "gain:");
    ORANGE(); printw(" %.2f", gain); CLR();

    YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= gain");
    mvprintw(y+2, x, "Command mode: :1 [gain]");
    BLACK();
}

static void mixer_handle_input(Module* m, int key) {
    MixerState* s = (MixerState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '-': s->gain -= 0.01f; handled = 1; break;
            case '=': s->gain += 0.01f; handled = 1; break;
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
                if (type == '1') s->gain = val;
            }
            handled = 1;
        } else if (key == 27) {
            s->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 && s->command_index < (int)sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled) clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void mixer_set_osc_param(Module* m, const char* param, float value) {
    MixerState* s = (MixerState*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "gain") == 0) {
        s->gain = value;
    } else {
        fprintf(stderr, "[mixer] Unknown OSC param: %s\n", param);
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void mixer_destroy(Module* m) {
    MixerState* s = (MixerState*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float gain = 1.0f;

    if (args && strstr(args, "gain=")) {
        sscanf(strstr(args, "gain="), "gain=%f", &gain);
    }

    MixerState* s = calloc(1, sizeof(MixerState));
    s->gain = gain;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_gain, 0.75f);
    clamp_params(s);

    s->display_gain = s->gain;

    Module* m = calloc(1, sizeof(Module));
    m->name = "mixer";
    m->state = s;

    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = mixer_process;
    m->draw_ui = mixer_draw_ui;
    m->handle_input = mixer_handle_input;
    m->set_param = mixer_set_osc_param;
    m->destroy = mixer_destroy;

    return m;
}

