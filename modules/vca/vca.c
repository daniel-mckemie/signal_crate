#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>
#include <stdio.h>

#include "module.h"
#include "util.h"
#include "vca.h"

static void vca_process(Module* m, float* in, unsigned long frames) {
    VCAState* s = (VCAState*)m->state;

    float* outL = m->output_bufferL;
    float* outR = m->output_bufferR;
    float* out  = m->output_buffer;

    float* input = (m->num_inputs > 0 && m->inputs[0]) ? m->inputs[0] : in;

    if (!input) {
        memset(outL, 0, frames * sizeof(float));
        memset(outR, 0, frames * sizeof(float));
        if (out) memset(out, 0, frames * sizeof(float));
        return;
    }

    pthread_mutex_lock(&s->lock);
    float base_gain = s->gain;
    float base_pan  = s->pan;
    pthread_mutex_unlock(&s->lock);

    float gain_s = process_smoother(&s->smooth_gain, base_gain);
    float pan_s  = process_smoother(&s->smooth_pan, base_pan);

    float disp_gain = gain_s;
    float disp_pan  = pan_s;

    for (unsigned long i = 0; i < frames; i++) {
        float gain = gain_s;
        float pan  = pan_s;

        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "gain") == 0) gain += control;
            else if (strcmp(param, "pan") == 0) pan += control;
        }

        clampf(&gain, 0.0f, 1.0f);
        clampf(&pan, -1.0f, 1.0f);

        disp_gain = gain;
        disp_pan  = pan;

        float x = input[i];
        float y = gain * x;

        if (out) out[i] = y;

        float angle = (pan + 1.0f) * (float)M_PI_4;
        float lg = cosf(angle);
        float rg = sinf(angle);

        outL[i] = lg * y;
        outR[i] = rg * y;
    }

    pthread_mutex_lock(&s->lock);
    s->display_gain = disp_gain;
    s->display_pan  = disp_pan;
    pthread_mutex_unlock(&s->lock);
}

static inline void clamp_params(VCAState* s) {
    clampf(&s->gain, 0.0f, 1.0f);
    clampf(&s->pan, -1.0f, 1.0f);
}

static void vca_draw_ui(Module* m, int y, int x) {
    VCAState* state = (VCAState*)m->state;

    pthread_mutex_lock(&state->lock);
    float gain = state->display_gain;
    float pan  = state->display_pan;
    pthread_mutex_unlock(&state->lock);

    BLUE();
    mvprintw(y, x, "[VCA:%s] ", m->name);
    CLR();

    LABEL(2, "gain:");
    ORANGE(); printw(" %.2f | ", gain); CLR();

    LABEL(2, "pan:");
    ORANGE(); printw(" %.2f | ", pan); CLR();

    YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= gain, [/] pan");
    mvprintw(y+2, x, "Command mode: :1 [gain], :2 [pan]");
    BLACK();
}

static void vca_handle_input(Module* m, int key) {
    VCAState* state = (VCAState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '-': state->gain -= 0.01f; handled = 1; break;
            case '=': state->gain += 0.01f; handled = 1; break;
            case '[': state->pan  -= 0.01f; handled = 1; break;
            case ']': state->pan  += 0.01f; handled = 1; break;
            case ':':
                state->entering_command = true;
                memset(state->command_buffer, 0, sizeof(state->command_buffer));
                state->command_index = 0;
                handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            state->entering_command = false;
            char type;
            float val;
            if (sscanf(state->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') state->gain = val;
                else if (type == '2') state->pan = val;
            }
            handled = 1;
        } else if (key == 27) {
            state->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && state->command_index > 0) {
            state->command_index--;
            state->command_buffer[state->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 && state->command_index < sizeof(state->command_buffer) - 1) {
            state->command_buffer[state->command_index++] = (char)key;
            state->command_buffer[state->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled) clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void vca_set_osc_param(Module* m, const char* param, float value) {
    VCAState* state = (VCAState*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "gain") == 0) {
        state->gain = fmaxf(value, 0.0f);
    } else if (strcmp(param, "pan") == 0) {
        float p = fminf(fmaxf(value, 0.0f), 1.0f);
        state->pan = p * 2.0f - 1.0f;
    }

    clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void vca_destroy(Module* m) {
    VCAState* state = (VCAState*)m->state;
    if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float gain = 1.0f;
    float pan  = 0.0f;
    int ch = -1;

    if (args && strstr(args, "gain=")) sscanf(strstr(args, "gain="), "gain=%f", &gain);
    if (args && strstr(args, "pan="))  sscanf(strstr(args, "pan="),  "pan=%f",  &pan);
    if (args && strstr(args, "ch="))   sscanf(strstr(args, "ch="),   "ch=%d",   &ch);

    VCAState* state = calloc(1, sizeof(VCAState));
    state->gain = gain;
    state->pan  = pan;
    state->target_channel = ch;

    pthread_mutex_init(&state->lock, NULL);
    init_smoother(&state->smooth_gain, 0.75f);
    init_smoother(&state->smooth_pan, 0.75f);
    clamp_params(state);

    Module* m = calloc(1, sizeof(Module));
    m->name = "vca";
    m->state = state;

    m->output_buffer  = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->output_bufferL = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->output_bufferR = calloc(MAX_BLOCK_SIZE, sizeof(float));

    m->process = vca_process;
    m->draw_ui = vca_draw_ui;
    m->handle_input = vca_handle_input;
    m->set_param = vca_set_osc_param;
    m->destroy = vca_destroy;

    (void)sample_rate;
    return m;
}

