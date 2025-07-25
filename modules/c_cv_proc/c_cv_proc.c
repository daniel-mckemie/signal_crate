#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "module.h"
#include "util.h"
#include "c_cv_proc.h"

static void c_cv_proc_process_control(Module* m) {
    CCVProc* s = (CCVProc*)m->state;

    float va = 0.0f, vb = 0.0f, vc = 0.0f;
    if (m->control_inputs[0]) va = *(m->control_inputs[0]);
    if (m->control_inputs[1]) vb = *(m->control_inputs[1]);
    if (m->control_inputs[2]) vc = *(m->control_inputs[2]);

    pthread_mutex_lock(&s->lock);
	float k = (m->control_inputs[3]) ? *(m->control_inputs[3]) : s->k;
	float m_amt = (m->control_inputs[4]) ? *(m->control_inputs[4]) : s->m;
	float offset = (m->control_inputs[5]) ? *(m->control_inputs[5]) : s->offset;

	// Clamp params
	k = fminf(fmaxf(k, -2.0f), 2.0f);
	m_amt = fminf(fmaxf(m_amt, 0.0f), 1.0f);
	offset = fminf(fmaxf(offset, -1.0f), 1.0f);

    float out = va * k + vb * (1.0f - m_amt) + vc * m_amt + offset;
    out = fminf(fmaxf(out, -1.0f), 1.0f);
    s->output = out;
    s->display_va = va;
    s->display_vb = vb;
    s->display_vc = vc;
	s->display_k = k;
	s->display_m_amt = m_amt;
	s->display_offset = offset;
    pthread_mutex_unlock(&s->lock);

    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++) {
        m->control_output[i] = out;
    }
}

static void c_cv_proc_draw_ui(Module* m, int y, int x) {
    CCVProc* s = (CCVProc*)m->state;

    pthread_mutex_lock(&s->lock);
    float val = s->output;
    pthread_mutex_unlock(&s->lock);

    mvprintw(y,   x, "[CVProc:%s] Out: %.3f | va: %.3f | vb: %.3f | vc: %.3f", m->name, val, s->display_va, s->display_vb, s->display_vc);
    mvprintw(y+1, x, "K: %.2f | M: %.2f | Offset: %.2f", s->display_k, s->display_m_amt, s->display_offset);
    mvprintw(y+2, x, "Keys: k/K (-/+) | m/M (-/+) | o/O (-/+)");
}

static void c_cv_proc_handle_input(Module* m, int ch) {
    CCVProc* s = (CCVProc*)m->state;

    pthread_mutex_lock(&s->lock);
    switch (ch) {
        case 'k': s->k      = fmaxf(-2.0f, s->k - 0.05f); break;
        case 'K': s->k      = fminf( 2.0f, s->k + 0.05f); break;
        case 'm': s->m      = fmaxf( 0.0f, s->m - 0.05f); break;
        case 'M': s->m      = fminf( 1.0f, s->m + 0.05f); break;
        case 'o': s->offset = fmaxf(-1.0f, s->offset - 0.05f); break;
        case 'O': s->offset = fminf( 1.0f, s->offset + 0.05f); break;
    }
    pthread_mutex_unlock(&s->lock);
}

static void c_cv_proc_set_osc_param(Module* m, const char* param, float value) {
    CCVProc* s = (CCVProc*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "k") == 0) {
        s->k = fminf(fmaxf(value, -2.0f), 2.0f);         // Gain for va
    } else if (strcmp(param, "m") == 0) {
        s->m = fminf(fmaxf(value, 0.0f), 1.0f);          // Crossfade between vb and vc
    } else if (strcmp(param, "offset") == 0) {
        s->offset = fminf(fmaxf(value, -1.0f), 1.0f);    // Output bias
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

Module* create_module(float sample_rate) {
    CCVProc* s = calloc(1, sizeof(CCVProc));
    s->k = 1.0f;
    s->m = 0.0f;
    s->offset = 0.0f;
    pthread_mutex_init(&s->lock, NULL);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_cv_proc";
    m->state = s;
    m->num_control_inputs = 6;
    m->process_control = c_cv_proc_process_control;
    m->draw_ui = c_cv_proc_draw_ui;
    m->handle_input = c_cv_proc_handle_input;
    m->set_param = c_cv_proc_set_osc_param;
    m->control_output = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->control_input_params[0] = "in";
    m->control_input_params[1] = "vb";
    m->control_input_params[2] = "vc";
	m->control_input_params[3] = "k";
	m->control_input_params[4] = "m";
	m->control_input_params[5] = "offset";
	m->destroy = c_cv_proc_destroy;

    return m;
}
