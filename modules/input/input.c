#include <stdlib.h>
#include <string.h>
#include <ncurses.h>
#include <pthread.h>

#include "module.h"
#include "util.h"
#include "input.h"

static void clamp_params(InputState* state) {
    if (state->amp < 0.0f) state->amp = 0.0f;
    if (state->amp > 1.0f) state->amp = 1.0f;
}

static void input_process(Module* m, float* in, unsigned long frames) {
    InputState* state = (InputState*)m->state;
    pthread_mutex_lock(&state->lock);
    float amp = process_smoother(&state->smooth_amp, state->amp);
    pthread_mutex_unlock(&state->lock);

    for (unsigned long i = 0; i < frames; i++) {
        m->output_buffer[i] = (in != NULL) ? amp * in[i] : 0.0f;
    }
}

static void input_draw_ui(Module* m, int row) {
    InputState* state = (InputState*)m->state;
    float amp;
    char cmd[128] = "";

    pthread_mutex_lock(&state->lock);
    amp = state->amp;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

    mvprintw(row,   2, "[Input] System Audio Input");
    mvprintw(row+1, 2, "        Amplitude: %.2f", amp);
    mvprintw(row+2, 2, "Real-time keys: _/+ (Amp)");
    mvprintw(row+3, 2, "Command mode: :1 [Amp]");
    if (state->entering_command)
        mvprintw(row+4, 2, "%s", cmd);
}

static void input_handle_input(Module* m, int key) {
    InputState* state = (InputState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);
    if (!state->entering_command) {
        switch (key) {
            case '+': state->amp += 0.05f; handled = 1; break;
            case '_': state->amp -= 0.05f; handled = 1; break;
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
                if (type == '1') state->amp = val;
            }
            handled = 1;
        } else if (key == 27) {  // ESC
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

    if (handled)
        clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void input_destroy(Module* m) {
    if (!m) return;
    InputState* state = (InputState*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
    InputState* state = calloc(1, sizeof(InputState));
    state->amp = 1.0f;
    state->sample_rate = sample_rate;
    init_smoother(&state->smooth_amp, 0.75f);
    pthread_mutex_init(&state->lock, NULL);
    clamp_params(state);

    Module* m = calloc(1, sizeof(Module));
    m->name = "input";
    m->state = state;
    m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = input_process;
    m->draw_ui = input_draw_ui;
    m->handle_input = input_handle_input;
    m->destroy = input_destroy;
    return m;
}

