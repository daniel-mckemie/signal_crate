#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_sh.h"
#include "module.h"
#include "util.h"

static void c_sh_process(Module* m, float* in, unsigned long frames) {
    CSH* s = (CSH*)m->state;
    float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
    float* out   = m->control_output;

    pthread_mutex_lock(&s->lock);
    float base_rate  = s->rate_hz;
    float base_depth = s->depth;
    pthread_mutex_unlock(&s->lock);

    float rate_s  = process_smoother(&s->smooth_rate,  base_rate);
    float depth_s = process_smoother(&s->smooth_depth, base_depth);

	float disp_rate = rate_s;
	float disp_depth = depth_s;

    float sr = s->sample_rate;
    float dt = 1.0f / sr;

    float last_trig = s->last_trig;
    float phase     = s->phase;

    float* trig = NULL;
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (m->control_input_params[j] &&
            strcmp(m->control_input_params[j], "trig") == 0)
            trig = m->control_inputs[j];
    }

    for (unsigned long i=0; i<frames; i++) {
        float rate  = rate_s;
        float depth = depth_s;

        for (int j=0; j<m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "rate") == 0) {
                rate += control * 20.0f;
            } else if (strcmp(param, "depth") == 0) {
                depth += control;
			}
        }

        clampf(&rate,  0.01f, 100.0f);
        clampf(&depth, 0.0f,  1.0f);

        int triggered = 0;

        if (trig) {
            float x = trig[i];
            if (last_trig < 0.5f && x >= 0.5f)
                triggered = 1;
            last_trig = x;
        } else {
            phase += dt * rate;
            if (phase >= 1.0f) {
                phase -= 1.0f;
                triggered = 1;
            }
        }

        if (triggered) {
            float sample = input ? input[i] : 0.0f;
            float v = sample * depth;
            s->current_val = v;

            pthread_mutex_lock(&s->lock);
            s->display_val = v;
            pthread_mutex_unlock(&s->lock);
        }

        out[i] = s->current_val;

		disp_rate = rate;
		disp_depth = depth;
    }

    s->last_trig = last_trig;
    s->phase     = phase;

    pthread_mutex_lock(&s->lock);
    s->display_rate  = disp_rate;
    s->display_depth = disp_depth;
    pthread_mutex_unlock(&s->lock);
}

static inline void clamp_params(CSH* s) {
    clampf(&s->rate_hz, 0.01f, 100.0f);
    clampf(&s->depth,   0.0f, 1.0f);
}

static void c_sh_draw_ui(Module* m, int y, int x) {
    CSH* s = (CSH*)m->state;

    float val, rate, depth;

    pthread_mutex_lock(&s->lock);
    val   = s->display_val;
    rate  = s->display_rate;
    depth = s->display_depth;
    pthread_mutex_unlock(&s->lock);

    BLUE(); mvprintw(y, x, "[S&H:%s] ", m->name); CLR();
    LABEL(2,"rate:");   ORANGE(); printw(" %.2f Hz | ", rate);   CLR();
    LABEL(2,"depth:");   ORANGE(); printw(" %.2f | ",   depth);   CLR();
    LABEL(2,"val:");   ORANGE(); printw(" %.3f",      val);     CLR();

    YELLOW();
    mvprintw(y+1,x, "-/= rate, d/D depth");
    mvprintw(y+2,x, "Cmd: :1 rate, :d depth");
    BLACK();
}

static void c_sh_handle_input(Module* m, int key) {
    CSH* s = (CSH*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->rate_hz += 0.1f; handled = 1; break;
            case '-': s->rate_hz -= 0.1f; handled = 1; break;
            case 'D': s->depth   += 0.01f; handled = 1; break;
            case 'd': s->depth   -= 0.01f; handled = 1; break;

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

            char t;
            float v;
            if (sscanf(s->command_buffer, "%c %f", &t, &v) == 2) {
                if      (t == '1') s->rate_hz = v;
                else if (t == 'd') s->depth   = v;
            }
            handled = 1;
        }
        else if (key == 27) { // ESC
            s->entering_command = false;
            handled = 1;
        }
        else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
        else if (key >= 32 && key < 127 &&
                 s->command_index < (int)sizeof(s->command_buffer) - 1)
        {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index]   = '\0';
            handled = 1;
        }
    }

    if (handled)
        clamp_params(s);

    pthread_mutex_unlock(&s->lock);
}

static void c_sh_set_osc_param(Module* m, const char* param, float value) {
    CSH* s = (CSH*)m->state;

    pthread_mutex_lock(&s->lock);

    if      (!strcmp(param, "rate"))  s->rate_hz = value;
    else if (!strcmp(param, "depth")) s->depth   = value;

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_sh_destroy(Module* m) {
    CSH* s = (CSH*)m->state;
    if (s)
        pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float rate  = 1.0f;
    float depth = 1.0f;

    if (args && strstr(args,"rate="))
        sscanf(strstr(args,"rate="),  "rate=%f",  &rate);
    if (args && strstr(args,"depth="))
        sscanf(strstr(args,"depth="), "depth=%f", &depth);

    CSH* s = calloc(1, sizeof(CSH));
    s->rate_hz     = rate;
    s->depth       = depth;
    s->sample_rate = sample_rate;
    s->phase       = 0.0f;
    s->current_val = 0.0f;
    s->display_val = 0.0f;
	s->display_rate = 0.0f;
	s->display_depth = 0.0f;
    s->last_trig   = 0.0f;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_rate,  0.75f);
    init_smoother(&s->smooth_depth, 0.75f);
    clamp_params(s);

    Module* m = calloc(1, sizeof(Module));
    m->name   = "c_sh";
    m->state  = s;

    m->output_buffer  = NULL; // no audio out
    m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));

    m->process         = c_sh_process;   // AUDIO-IN → CONTROL-OUT
    m->process_control = NULL;

    m->draw_ui      = c_sh_draw_ui;
    m->handle_input = c_sh_handle_input;
    m->set_param    = c_sh_set_osc_param;
    m->destroy      = c_sh_destroy;

    m->num_control_inputs = 1;

    return m;
}

