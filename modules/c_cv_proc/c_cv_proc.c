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
	float* out = m->control_output;

    float base_k, base_m, base_offset;
    pthread_mutex_lock(&s->lock);
    base_k      = s->k;
    base_m      = s->m;
    base_offset = s->offset;
    pthread_mutex_unlock(&s->lock);

    float k_s      = process_smoother(&s->smooth_k,      base_k);
    float m_s      = process_smoother(&s->smooth_m,      base_m);
    float offset_s = process_smoother(&s->smooth_offset, base_offset);

	float disp_k      = k_s;
	float disp_m      = m_s;
	float disp_offset = offset_s;
	float disp_va = 0.0f, disp_vb = 0.0f, disp_vc = 0.0f;
	float disp_out = 0.0f;


	for (unsigned long i=0; i<frames; i++) {
		float k = k_s;
		float m_amt = m_s;
		float offset = offset_s;
		
		for (int j = 0; j < m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "k") == 0) {
				float range = fabsf(base_k) > 0.0f ? fabsf(base_k) : 2.0f;
				k += control * range;
			} else if (strcmp(param, "m") == 0) {
				m_amt += control * (1.0f - base_m);
			} else if (strcmp(param, "offset") == 0) {
				offset += control * (1.0f - fabsf(base_offset));
			}
		}

		clampf(&k, -2.0f, 2.0f);
		clampf(&m_amt, 0.0f, 1.0f);
		clampf(&offset, -1.0f, 1.0f);

		float va = (m->num_control_inputs > 0 && m->control_inputs[0]) ? m->control_inputs[0][i] : 0.0f;
		float vb = (m->num_control_inputs > 1 && m->control_inputs[1]) ? m->control_inputs[1][i] : 0.0f;
		float vc = (m->num_control_inputs > 2 && m->control_inputs[2]) ? m->control_inputs[2][i] : 0.0f;

		float val = va * k + vb * (1.0f - m_amt) + vc * m_amt + offset;
		val = fminf(fmaxf(val, -1.0f), 1.0f);

		out[i] = val;

		disp_k = k;
		disp_m = m_amt;
		disp_offset = offset;
		disp_va = va;
		disp_vb = vb;
		disp_vc = vc;
		disp_out = val;
	}

    pthread_mutex_lock(&s->lock);
    s->output         = disp_out;
    s->display_va     = disp_va;
    s->display_vb     = disp_vb;
    s->display_vc     = disp_vc;
    s->display_k      = disp_k;
    s->display_m_amt  = disp_m;
    s->display_offset = disp_offset;
    pthread_mutex_unlock(&s->lock);

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
    mvprintw(y+2, x, "Keys: :1/k -/= :2/m _/+, :3/offset [/]");
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
