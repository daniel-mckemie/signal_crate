#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_function.h"
#include "module.h"
#include "util.h"

static void c_function_process_control(Module* m, unsigned long frames) {
    CFunction* s = (CFunction*)m->state;
    float* out   = m->control_output;

    float base_att, base_rel, base_depth;
    int   base_cycle;
    float th_trig, th_gate, th_cycle;
    bool  short_mode;
    bool  stop_req;

    pthread_mutex_lock(&s->lock);
    base_att   = s->attack_time;
    base_rel   = s->release_time;
    base_depth = s->depth;
    base_cycle = s->cycle;
    th_trig    = s->threshold_trigger;
    th_gate    = s->threshold_gate;
    th_cycle   = s->threshold_cycle;
    short_mode = s->short_mode;
    stop_req   = s->cycle_stop_requested;
    pthread_mutex_unlock(&s->lock);

    float att_s   = process_smoother(&s->smooth_att,   base_att);
    float rel_s   = process_smoother(&s->smooth_rel,   base_rel);
    float depth_s = process_smoother(&s->smooth_depth, base_depth);

    float* gate_buf  = NULL;
    float* trig_buf  = NULL;
    float* cycle_buf = NULL;

    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;
        const char* p = m->control_input_params[j];
        if      (!gate_buf  && strcmp(p, "gate")  == 0) gate_buf  = m->control_inputs[j];
        else if (!trig_buf  && strcmp(p, "trig")  == 0) trig_buf  = m->control_inputs[j];
        else if (!cycle_buf && strcmp(p, "cycle") == 0) cycle_buf = m->control_inputs[j];
    }

    float disp_att   = att_s;
    float disp_rel   = rel_s;
    float disp_depth = depth_s;

    bool prev_gate = s->gate_prev;
    bool prev_trig = s->trig_prev;
    bool prev_cyc  = s->cycle_prev_cv;

    float sr   = (s->sample_rate > 0.0f) ? s->sample_rate : 48000.0f;
    float step = 1.0f / sr;

    int cycle = base_cycle;
    if (!cycle && stop_req) stop_req = false;

    for (unsigned long i = 0; i < frames; i++) {

        float att   = att_s;
        float rel   = rel_s;
        float depth = depth_s;

        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* p = m->control_input_params[j];
            if (strcmp(p, "gate") == 0 || strcmp(p, "trig") == 0 || strcmp(p, "cycle") == 0)
                continue;

            float cv = fminf(fmaxf(m->control_inputs[j][i], -1.0f), 1.0f);

            if (strcmp(p, "att") == 0) {
                float max = short_mode ? 10.0f : 1000.0f;
                att += cv * (max - base_att);
            } else if (strcmp(p, "rel") == 0) {
                float max = short_mode ? 10.0f : 1000.0f;
                rel += cv * (max - base_rel);
            } else if (strcmp(p, "depth") == 0) {
                depth += cv * (1.0f - base_depth);
            }
        }

        if (short_mode) {
            clampf(&att, 0.01f, 10.0f);
            clampf(&rel, 0.01f, 10.0f);
        } else {
            clampf(&att, 0.01f, INFINITY);
            clampf(&rel, 0.01f, INFINITY);
        }
        clampf(&depth, 0.0f, 1.0f);

        bool gate_now = gate_buf ? (gate_buf[i] > th_gate) : false;
        bool trig_now = trig_buf ? (trig_buf[i] > th_trig) : false;

        if (cycle_buf) {
            bool cyc_now = (cycle_buf[i] > th_cycle);
            if (cyc_now && !prev_cyc) {
                cycle = 1;
                stop_req = false;
            } else if (!cyc_now && prev_cyc && cycle) {
                stop_req = true;
            }
            prev_cyc = cyc_now;
        }

        bool fire = (trig_now && !prev_trig) || (gate_now && !prev_gate);

        if (fire && s->state == ENV_IDLE)
            s->state = ENV_ATTACK;

        switch (s->state) {

            case ENV_ATTACK: {
                float delta = step / fmaxf(att, 0.001f);
                s->envelope_out += delta;
                if (s->envelope_out >= 1.0f) {
                    s->envelope_out = 1.0f;
                    s->state = ENV_RELEASE;
                }
                break;
            }

            case ENV_RELEASE: {
                float delta = step / fmaxf(rel, 0.001f);
                s->envelope_out -= delta;
                if (s->envelope_out <= 0.0f) {
                    s->envelope_out = 0.0f;
                    if (cycle && !stop_req) {
                        s->state = ENV_ATTACK;
                    } else {
                        cycle = 0;
                        stop_req = false;
                        s->state = ENV_IDLE;
                    }
                }
                break;
            }

            case ENV_IDLE:
            default:
                if (cycle && !stop_req) {
                    s->state = ENV_ATTACK;
                } else {
                    s->envelope_out = 0.0f;
                }
                break;
        }

        out[i] = s->envelope_out * depth;

        disp_att   = att;
        disp_rel   = rel;
        disp_depth = depth;

        prev_gate = gate_now;
        prev_trig = trig_now;
    }

    s->gate_prev     = prev_gate;
    s->trig_prev     = prev_trig;
    s->cycle_prev_cv = prev_cyc;

    pthread_mutex_lock(&s->lock);
    s->display_att          = disp_att;
    s->display_rel          = disp_rel;
    s->display_depth        = disp_depth;
    s->display_cycle        = cycle;
    s->cycle                = cycle;
    s->cycle_stop_requested = stop_req;
    pthread_mutex_unlock(&s->lock);
}

static void clamp_params(CFunction* s) {
    if (s->short_mode) {
        clampf(&s->attack_time,  0.01f, 10.0f);
        clampf(&s->release_time, 0.01f, 10.0f);
    } else {
        clampf(&s->attack_time,  0.01f, INFINITY);
        clampf(&s->release_time, 0.01f, INFINITY);
    }
    clampf(&s->depth,             0.0f, 1.0f);
    clampf(&s->threshold_trigger, 0.0f, 1.0f);
    clampf(&s->threshold_gate,    0.0f, 1.0f);
    clampf(&s->threshold_cycle,   0.0f, 1.0f);
}

static void c_function_draw_ui(Module* m, int y, int x) {
    CFunction* s = (CFunction*)m->state;
    pthread_mutex_lock(&s->lock);

    BLUE();
    mvprintw(y, x, "[Function:%s] ", m->name);
    CLR();

    LABEL(2, "att:");
    ORANGE(); printw(" %.2fs|", s->display_att); CLR();

    LABEL(2, "rel:");
    ORANGE(); printw(" %.2fs|", s->display_rel); CLR();

    LABEL(2, "gate:");
    ORANGE(); printw(" %.2f|", s->threshold_gate); CLR();

    LABEL(2, "depth:");
    ORANGE(); printw(" %.2f|", s->display_depth); CLR();

    ORANGE(); printw("%s|", s->short_mode ? "s" : "l"); CLR();
    ORANGE(); printw("%s", s->display_cycle ? "c" : "t"); CLR();

    YELLOW();
    mvprintw(y+1, x, "Keys: fire/cycle f/c, att -/=, rel _/+, gate [/], depth d/D, sh/lng [m]");
    mvprintw(y+2, x, "Command: :1 [att], :2 [rel], :3 [g_thresh], :d[depth]");
    pthread_mutex_unlock(&s->lock);
    BLACK();
}

static void c_function_handle_input(Module* m, int key) {
    CFunction* s = (CFunction*)m->state;
    int handled = 0;
    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {

            /* Trigger once (does not force state; just sets intent via stop flag + one-shot) */
            case 'f':
                /* if cycling, request stop after current release */
                if (s->cycle) {
                    s->cycle_stop_requested = true;
                } else {
                    /* one-shot trigger is done by nudging trig_prev edge detector */
                    s->trig_prev = false;
                }
                handled = 1;
                break;

            /* Cycle toggle: request stop-after-release; do NOT touch display_cycle */
            case 'c':
                if (!s->cycle) {
                    s->cycle = true;
                    s->cycle_stop_requested = false;
                } else {
                    s->cycle_stop_requested = true;
                }
                handled = 1;
                break;

            case 'm': s->short_mode = !s->short_mode; handled = 1; break;
            case '-': s->attack_time -= 0.1f; handled = 1; break;
            case '=': s->attack_time += 0.1f; handled = 1; break;
            case '_': s->release_time -= 0.1f; handled = 1; break;
            case '+': s->release_time += 0.1f; handled = 1; break;
            case '[': s->threshold_gate -= 0.05f; handled = 1; break;
            case ']': s->threshold_gate += 0.05f; handled = 1; break;
            case 'd': s->depth -= 0.01f; handled = 1; break;
            case 'D': s->depth += 0.01f; handled = 1; break;

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
                if (type == '1') s->attack_time = val;
                else if (type == '2') s->release_time = val;
                else if (type == '3') s->threshold_gate = val;
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
        } else if (key >= 32 && key < 127 && s->command_index < (int)sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled) clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_function_set_osc_param(Module* m, const char* param, float value) {
    CFunction* s = (CFunction*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "att") == 0) {
        s->attack_time = value * 1000.0f;
    } else if (strcmp(param, "rel") == 0) {
        s->release_time = value * 1000.0f;
    } else if (strcmp(param, "depth") == 0) {
        s->depth = value;
    } else if (strcmp(param, "gate") == 0) {
        s->threshold_gate = value;
    } else if (strcmp(param, "cycle") == 0) {
        /* 0..1: treat as toggle-intent, not display */
        if (value > 0.5f) {
            s->cycle = true;
            s->cycle_stop_requested = false;
        } else {
            s->cycle_stop_requested = true;
        }
    } else if (strcmp(param, "trig") == 0) {
        /* trigger intent without forcing engine state */
        if (value > s->threshold_trigger) {
            s->trig_prev = false;
        }
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_function_destroy(Module* m) {
    CFunction* s = (CFunction*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float attack_time  = 1.0f;
    float release_time = 1.0f;
    float depth        = 0.5f;

    if (args && strstr(args, "att="))   sscanf(strstr(args, "att="),   "att=%f",   &attack_time);
    if (args && strstr(args, "rel="))   sscanf(strstr(args, "rel="),   "rel=%f",   &release_time);
    if (args && strstr(args, "depth=")) sscanf(strstr(args, "depth="), "depth=%f", &depth);

    CFunction* s = calloc(1, sizeof(CFunction));
    s->attack_time = attack_time;
    s->release_time = release_time;
    s->depth = depth;

    s->sample_rate = sample_rate;
    s->short_mode = true;

    s->threshold_trigger = 0.5f;
    s->threshold_gate    = 0.5f;
    s->threshold_cycle   = 0.5f;

    s->envelope_out = 0.0f;
    s->state = ENV_IDLE;

    s->gate_prev = false;
    s->trig_prev = false;
    s->cycle_prev_cv = false;

    s->cycle = false;
    s->cycle_stop_requested = false;

    pthread_mutex_init(&s->lock, NULL);

    init_smoother(&s->smooth_att,   0.75f);
    init_smoother(&s->smooth_rel,   0.75f);
    init_smoother(&s->smooth_depth, 0.75f);

    clamp_params(s);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_function";
    m->state = s;
    m->process_control = c_function_process_control;
    m->draw_ui = c_function_draw_ui;
    m->handle_input = c_function_handle_input;
    m->set_param = c_function_set_osc_param;
    m->destroy = c_function_destroy;
    m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
    return m;
}

