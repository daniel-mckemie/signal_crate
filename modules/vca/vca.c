#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "module.h"
#include "util.h"
#include "vca.h"

static inline float clamp01(float x) { return fminf(fmaxf(x, 0.0f), 1.0f); }
static inline float clamp11(float x) { return fminf(fmaxf(x, -1.0f), 1.0f); }

static inline void clamp_params(VCAState* s) {
    clampf(&s->gain, 0.0f, 1.0f);
    clampf(&s->pan, -1.0f, 1.0f);
}

static void vca_process(Module* m, float* in, unsigned long frames) {
    VCAState* s = (VCAState*)m->state;
    float* outL = m->output_bufferL;
    float* outR = m->output_bufferR;

    float base_gain, base_pan;

    pthread_mutex_lock(&s->lock);
    base_gain = s->gain;
    base_pan  = s->pan;
    pthread_mutex_unlock(&s->lock);

    base_gain = clamp01(base_gain);
    base_pan  = clamp11(base_pan);

    const float* gain_cv = NULL;
    const float* pan_cv  = NULL;

    for (int ci = 0; ci < m->num_control_inputs; ci++) {
        if (!m->control_inputs[ci] || !m->control_input_params[ci]) continue;
        if (!gain_cv && strcmp(m->control_input_params[ci], "gain") == 0)
            gain_cv = m->control_inputs[ci];
        else if (!pan_cv && strcmp(m->control_input_params[ci], "pan") == 0)
            pan_cv = m->control_inputs[ci];
    }

    float last_gain = s->gain_prev;
    float last_pan  = base_pan;

    for (unsigned long i = 0; i < frames; i++) {
        float x = in ? in[i] : 0.0f;

        float gain_t = base_gain;
        if (gain_cv) {
            float c = gain_cv[i];
            if (c < 0.0f) c = 0.5f * (clamp11(c) + 1.0f);
            gain_t = clamp01(c);
        }

        if (fabsf(gain_t - s->gain_prev) > 1e-6f) {
            s->gain_step = (gain_t - s->gain_prev) * 0.015625f;
        }

        s->gain_prev += s->gain_step;

        if ((s->gain_step > 0 && s->gain_prev >= gain_t) ||
            (s->gain_step < 0 && s->gain_prev <= gain_t)) {
            s->gain_prev = gain_t;
            s->gain_step = 0.0f;
        }

        float pan_t = base_pan;
        if (pan_cv) {
            float c = clamp11(pan_cv[i]);
            pan_t = base_pan + c * (1.0f - fabsf(base_pan));
            pan_t = clamp11(pan_t);
        }

        float pan_s = process_smoother(&s->smooth_pan, pan_t);

        float angle = (pan_s + 1.0f) * (float)M_PI_4;
        float lg = cosf(angle);
        float rg = sinf(angle);

        outL[i] = s->gain_prev * lg * x;
        outR[i] = s->gain_prev * rg * x;

        last_gain = s->gain_prev;
        last_pan  = pan_s;
    }

    pthread_mutex_lock(&s->lock);
    s->display_gain = last_gain;
    s->display_pan  = last_pan;
    pthread_mutex_unlock(&s->lock);
}

static void vca_draw_ui(Module* m, int y, int x) {
    VCAState* s = (VCAState*)m->state;

    float gain, pan;
    pthread_mutex_lock(&s->lock);
    gain = s->display_gain;
    pan  = s->display_pan;
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[VCA:%s] ", m->name);
    CLR();

    LABEL(2, "pan:");
    ORANGE(); printw(" %.2f | ", pan); CLR();

    LABEL(2, "gain:");
    ORANGE(); printw(" %.2f | ", gain); CLR();

    YELLOW();
    mvprintw(y+1, x, "Keys: -/= pan, [/] gain");
    mvprintw(y+2, x, "Cmd: :1 [pan], :2 [gain]");
    BLACK();
}

static void vca_handle_input(Module* m, int key) {
    VCAState* s = (VCAState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '-': s->pan  -= 0.01f; handled = 1; break;
            case '=': s->pan  += 0.01f; handled = 1; break;
            case '[': s->gain -= 0.01f; handled = 1; break;
            case ']': s->gain += 0.01f; handled = 1; break;
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
                if (t == '1') s->pan = v;
                else if (t == '2') s->gain = v;
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

    if (handled) {
        clamp_params(s);
        s->display_gain = s->gain;
        s->display_pan  = s->pan;
    }

    pthread_mutex_unlock(&s->lock);
}

static void vca_set_osc_param(Module* m, const char* param, float value) {
    VCAState* s = (VCAState*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "gain") == 0)
        s->gain = value;
    else if (strcmp(param, "pan") == 0)
        s->pan = clamp01(value) * 2.0f - 1.0f;

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void vca_destroy(Module* m) {
    VCAState* s = (VCAState*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    (void)sample_rate;

    float gain = 1.0f;
    float pan  = 0.0f;
    int ch = -1;

    if (args && strstr(args, "gain=")) sscanf(strstr(args, "gain="), "gain=%f", &gain);
    if (args && strstr(args, "pan="))  sscanf(strstr(args, "pan="),  "pan=%f",  &pan);
    if (args && strstr(args, "ch="))   sscanf(strstr(args, "ch="),   "ch=%d",   &ch);

    VCAState* s = calloc(1, sizeof(VCAState));
    s->gain = gain;
    s->pan  = pan;
    s->target_channel = ch;
    s->gain_prev = clamp01(gain);
    s->gain_step = 0.0f;

    pthread_mutex_init(&s->lock, NULL);

    init_smoother(&s->smooth_pan, 0.75f);

    clamp_params(s);
    s->display_gain = s->gain;
    s->display_pan  = s->pan;

    Module* m = calloc(1, sizeof(Module));
    m->name = "vca";
    m->state = s;
    m->output_bufferL = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->output_bufferR = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = vca_process;
    m->draw_ui = vca_draw_ui;
    m->handle_input = vca_handle_input;
    m->set_param = vca_set_osc_param;
    m->destroy = vca_destroy;

    return m;
}

