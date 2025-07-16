#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "module.h"
#include "util.h"
#include "output.h"

static void output_process(Module* m, float* in, unsigned long frames) {
    OutputState* state = (OutputState*)m->state;
    float* out = m->output_buffer;

    pthread_mutex_lock(&state->lock);
    float gain = state->gain;
    pthread_mutex_unlock(&state->lock);

    for (unsigned long i = 0; i < frames; i++) {
        out[i] = gain * in[i];
    }
}

static void output_draw_ui(Module* m, int y, int x) {
    OutputState* state = (OutputState*)m->state;

    pthread_mutex_lock(&state->lock);
    float gain = state->gain;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[Output] Gain: %.2f", gain);
    mvprintw(y+1, x, "Real-time keys: -/= gain");
    mvprintw(y+2, x, "Command mode: :1 [gain]");
}

static void output_handle_input(Module* m, int key) {
    OutputState* state = (OutputState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '-': state->gain -= 0.05f; handled = 1; break;
            case '=': state->gain += 0.05f; handled = 1; break;
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

    // Clamp
    if (handled) {
        if (state->gain < 0.0f) state->gain = 0.0f;
        if (state->gain > 1.0f) state->gain = 1.0f;
    }

    pthread_mutex_unlock(&state->lock);
}

static void output_set_param(Module* m, const char* param, float value) {
    OutputState* state = (OutputState*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "gain") == 0) {
        state->gain = fmaxf(value, 0.0f);
    } else {
        fprintf(stderr, "[output] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&state->lock);
}

static void output_destroy(Module* m) {
    OutputState* state = (OutputState*)m->state;
    pthread_mutex_destroy(&state->lock);
    free(state);
}

Module* create_module(float sample_rate) {
    OutputState* state = calloc(1, sizeof(OutputState));
    state->gain = 1.0f;
    pthread_mutex_init(&state->lock, NULL);

    Module* m = calloc(1, sizeof(Module));
    m->name = "output";  // IMPORTANT: engine uses "out" for final audio
    m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));  // mono
    m->process = output_process;
    m->draw_ui = output_draw_ui;
    m->handle_input = output_handle_input;
    m->set_param = output_set_param;
    m->destroy = output_destroy;
    return m;
}

