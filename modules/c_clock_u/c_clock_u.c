#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_clock_u.h"
#include "module.h"
#include "util.h"

static void clamp_params(CClockU* s) {
    clampf(&s->bpm,  1.0f,    1000.0f);
    clampf(&s->mult, 0.0001f, 128.0f);
    clampf(&s->pw,   0.001f,  0.999f);
}

static void c_clock_process_control(Module* m) {
    CClockU* s = (CClockU*)m->state;
    float* out = m->control_output;
    if (!out) return;

    float bpm, mult, pw;
    int running;
    double phase;
    float sr;

    pthread_mutex_lock(&s->lock);
    bpm     = s->bpm;
    mult    = s->mult;
    pw      = s->pw;
    running = s->running;
    phase   = s->phase;
    sr      = s->sample_rate;
    pthread_mutex_unlock(&s->lock);

    float disp_bpm  = bpm;
    float disp_mult = mult;
    float disp_pw   = pw;
    float last_gate = s->last_gate;

    if (!running) {
        memset(out, 0, sizeof(float) * MAX_BLOCK_SIZE);
        pthread_mutex_lock(&s->lock);
        s->last_gate       = 0.0f;
        s->display_bpm     = disp_bpm;
        s->display_mult    = disp_mult;
        s->display_pw      = disp_pw;
        s->display_running = 0;
        pthread_mutex_unlock(&s->lock);
        return;
    }

    double freq = (double)bpm * (double)mult / 60.0;
    double phase_inc = freq / sr;

    for (unsigned long i = 0; i < MAX_BLOCK_SIZE; i++) {
        phase += phase_inc;
        if (phase >= 1.0)
            phase -= floor(phase);

        float gate = (phase < pw) ? 1.0f : 0.0f;
        out[i] = gate;
        last_gate = gate;
    }

    pthread_mutex_lock(&s->lock);
    s->phase           = phase;
    s->last_gate       = last_gate;
    s->display_bpm     = disp_bpm;
    s->display_mult    = disp_mult;
    s->display_pw      = disp_pw;
    s->display_running = running;
    pthread_mutex_unlock(&s->lock);
}

static void c_clock_draw_ui(Module* m, int y, int x) {
    CClockU* s = (CClockU*)m->state;

    pthread_mutex_lock(&s->lock);
    float bpm     = s->display_bpm;
    float mult    = s->display_mult;
    float pw      = s->display_pw;
    float gate    = s->last_gate;
    int   running = s->display_running;
    char  buf[64]; memcpy(buf, s->command_buffer, sizeof(buf));
    pthread_mutex_unlock(&s->lock);

    BLUE();   mvprintw(y,   x, "[CLK:%s] ", m->name); CLR();
    LABEL(2, "bpm:");  ORANGE(); printw(" %.1f | ", bpm);  CLR();
    LABEL(2, "mult:"); ORANGE(); printw(" %.2f | ", mult); CLR();
    LABEL(2, "pw:");   ORANGE(); printw(" %.2f | ", pw);   CLR();
    LABEL(2, "gate:"); ORANGE(); printw(" %d | ", (int)gate); CLR();
    LABEL(2, "run:");  ORANGE(); printw(" %s", running ? "on" : "off"); CLR();

    YELLOW();
    mvprintw(y+1, x, "Keys: -/= bpm,  _/+ mult,  [/] pw, SPACE run/stop");
    mvprintw(y+2, x, "Cmd: :1 [bpm], :2 [mult], :3 [pw]");
    CLR();
}

static void c_clock_handle_input(Module* m, int key) {
    CClockU* s = (CClockU*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '-': s->bpm  -= 1.0f; handled = 1; break;
            case '=': s->bpm  += 1.0f; handled = 1; break;
            case '_': s->mult *= 0.5f; handled = 1; break;
            case '+': s->mult *= 2.0f; handled = 1; break;
            case '[': s->pw   -= 0.01f; handled = 1; break;
            case ']': s->pw   += 0.01f; handled = 1; break;
            case ' ':
                s->running = !s->running;
                handled = 1;
                break;

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
                if (type == '1') s->bpm  = val;
                else if (type == '2') s->mult = val;
                else if (type == '3') s->pw   = val;
            }
            handled = 1;
        } else if (key == 27) { // ESC
            s->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 &&
                   s->command_index < (int)sizeof(s->command_buffer)-1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled) clamp_params(s);

    pthread_mutex_unlock(&s->lock);
}

static void c_clock_set_osc_param(Module* m, const char* param, float value) {
    CClockU* s = (CClockU*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "bpm") == 0) {
        s->bpm = value;
    } else if (strcmp(param, "mult") == 0) {
        s->mult = value;
    } else if (strcmp(param, "pw") == 0) {
        s->pw = value;
    } else if (strcmp(param, "run") == 0) {
        s->running = (value > 0.5f);
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_clock_destroy(Module* m) {
    CClockU* s = (CClockU*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float bpm  = 120.0f;
    float mult = 1.0f;
    float pw   = 0.5f;

    if (args && strstr(args, "bpm="))  sscanf(strstr(args, "bpm="),  "bpm=%f",  &bpm);
    if (args && strstr(args, "mult=")) sscanf(strstr(args, "mult="), "mult=%f", &mult);
    if (args && strstr(args, "pw="))   sscanf(strstr(args, "pw="),   "pw=%f",   &pw);

    CClockU* s = calloc(1, sizeof(CClockU));
    s->bpm         = bpm;
    s->mult        = mult;
    s->pw          = pw;
    s->display_bpm = bpm;
    s->display_mult = mult;
    s->display_pw  = pw;
    s->running     = 1;
    s->display_running = 1;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);

    Module* m = calloc(1, sizeof(Module));
    m->name            = "c_clock_u";
    m->state           = s;
    m->process_control = c_clock_process_control;
    m->draw_ui         = c_clock_draw_ui;
    m->handle_input    = c_clock_handle_input;
    m->set_param       = c_clock_set_osc_param;
    m->destroy         = c_clock_destroy;
    m->control_output  = calloc(MAX_BLOCK_SIZE, sizeof(float));
    return m;
}

