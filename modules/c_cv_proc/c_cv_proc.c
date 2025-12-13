#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "module.h"
#include "util.h"
#include "c_cv_proc.h"

static void c_cv_proc_process_control(Module* m, unsigned long frames) {
    CCVProc* s = (CCVProc*)m->state;

    // 1) Read base values (UI / OSC targets)
    float k_base, m_base, offset_base;
    pthread_mutex_lock(&s->lock);
    k_base      = s->k;
    m_base      = s->m;
    offset_base = s->offset;
    pthread_mutex_unlock(&s->lock);

    // 2) Smooth ONLY base params (no CV in this step)
    float k      = process_smoother(&s->smooth_k,      k_base);
    float m_amt  = process_smoother(&s->smooth_m,      m_base);
    float offset = process_smoother(&s->smooth_offset, offset_base);

    // 3) Apply CV modulation on top of smoothed params (no smoothing of CV)
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
        float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "k") == 0) {
            float mod_range = fabsf(k_base) > 0.0f ? fabsf(k_base) : 2.0f;
            k += norm * mod_range;
        } else if (strcmp(param, "m") == 0) {
            float mod_range = (1.0f - m_base);
            m_amt += norm * mod_range;
        } else if (strcmp(param, "offset") == 0) {
            float mod_range = (1.0f - fabsf(offset_base));
            offset += norm * mod_range;
        }
    }

    // 4) Clamp final values after CV
    k      = fminf(fmaxf(k,     -2.0f),  2.0f);
    m_amt  = fminf(fmaxf(m_amt,  0.0f),  1.0f);
    offset = fminf(fmaxf(offset,-1.0f),  1.0f);

    // 5) Read signal inputs (va, vb, vc)
    float va = 0.0f, vb = 0.0f, vc = 0.0f;
    if (m->control_inputs[0]) va = *(m->control_inputs[0]);
    if (m->control_inputs[1]) vb = *(m->control_inputs[1]);
    if (m->control_inputs[2]) vc = *(m->control_inputs[2]);

    // 6) Core CV processing
    float out = va * k + vb * (1.0f - m_amt) + vc * m_amt + offset;
    out = fminf(fmaxf(out, -1.0f), 1.0f);

    // 7) Store display state
    pthread_mutex_lock(&s->lock);
    s->output         = out;
    s->display_va     = va;
    s->display_vb     = vb;
    s->display_vc     = vc;
    s->display_k      = k;
    s->display_m_amt  = m_amt;
    s->display_offset = offset;
    pthread_mutex_unlock(&s->lock);

    // 8) Write constant control output for this block
    for (unsigned long i = 0; i < frames; i++) {
        m->control_output[i] = out;
    }
}

static void clamp_params(CCVProc* s) {
    clampf(&s->k,     -2.0f,  2.0f);
    clampf(&s->m,      0.0f,  1.0f);
    clampf(&s->offset, -1.0f, 1.0f);
}

static void c_cv_proc_draw_ui(Module* m, int y, int x) {
    CCVProc* s = (CCVProc*)m->state;

    pthread_mutex_lock(&s->lock);
    float val = s->output;
    pthread_mutex_unlock(&s->lock);

	BLUE();
    mvprintw(y,   x, "[CVProc:%s] ", m->name);
	CLR();

	LABEL(2, "k:");
	ORANGE(); printw(" %.2f | ", s->display_k); CLR();

	LABEL(2, "m:");
	ORANGE(); printw(" %.2f | ", s->display_m_amt); CLR();

	LABEL(2, "offset:");
	ORANGE(); printw(" %.2f", s->display_offset); CLR();

	YELLOW();
    mvprintw(y+1, x, "out: %.3f | va: %.3f | vb: %.3f | vc: %.3f", val, s->display_va, s->display_vb, s->display_vc);
    mvprintw(y+2, x, "Keys: k/:1 -/= m/:2 _/+, offset/:3 [/]");
	BLACK();
}

static void c_cv_proc_handle_input(Module* m, int key) {
    CCVProc* s = (CCVProc*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->k += 0.01f; handled = 1; break;
            case '-': s->k -= 0.01f; handled = 1; break;
            case '+': s->m += 0.01f; handled = 1; break;
            case '_': s->m -= 0.01f; handled = 1; break;
            case ']': s->offset += 0.01f; handled = 1; break;
            case '[': s->offset -= 0.01f; handled = 1; break;
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
                if (type == '1') s->k = val;
                else if (type == '2') s->m = val;
                else if (type == '3') s->offset = val;
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

    if (handled)
        clamp_params(s);

    pthread_mutex_unlock(&s->lock);
}

static void c_cv_proc_set_osc_param(Module* m, const char* param, float value) {
    CCVProc* s = (CCVProc*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "k") == 0) {
		float mapped = (value * 4.0f) - 2.0f;
        s->k = fminf(fmaxf(mapped, -2.0f), 2.0f);         // Gain for va
    } else if (strcmp(param, "m") == 0) {
        s->m = fminf(fmaxf(value, 0.0f), 1.0f);          // Crossfade between vb and vc
    } else if (strcmp(param, "offset") == 0) {
		float mapped = (value * 2.0f) - 1.0f;
        s->offset = fminf(fmaxf(mapped, -1.0f), 1.0f);    // Output bias
    } else {
        fprintf(stderr, "[c_cv_proc] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&s->lock);
}

static void c_cv_proc_destroy(Module* m) {
    CCVProc* s = (CCVProc*)m->state;
    if (s) {
        pthread_mutex_destroy(&s->lock);
    }
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float k = 1.0f;
	float m = 0.0f;
	float offset = 0.0f;

	if (args && strstr(args, "k=")) {
        sscanf(strstr(args, "k="), "k=%f", &k);
    }
    if (args && strstr(args, "m=")) {
        sscanf(strstr(args, "m="), "m=%f", &m);
	}
	if (args && strstr(args, "offset=")) {
        sscanf(strstr(args, "offset="), "offset=%f", &offset);
    }

	CCVProc* s = calloc(1, sizeof(CCVProc));
    s->k = k;
    s->m = m;
    s->offset = offset;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_k, 0.75f);
    init_smoother(&s->smooth_m, 0.75f);
    init_smoother(&s->smooth_offset, 0.75f);

    Module* mod = calloc(1, sizeof(Module));
    mod->name = "c_cv_proc";
    mod->state = s;
    mod->process_control = c_cv_proc_process_control;
    mod->draw_ui = c_cv_proc_draw_ui;
    mod->handle_input = c_cv_proc_handle_input;
    mod->set_param = c_cv_proc_set_osc_param;
    mod->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
    mod->destroy = c_cv_proc_destroy;

    return mod;
}
