#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "module.h"
#include "util.h"
#include "c_output.h"

static void c_output_process(Module* m, float* in, unsigned long frames) {
    COutputState* s = (COutputState*)m->state;
    float* outL = m->output_bufferL;
    float* outR = m->output_bufferR;
    if (!outL || !outR) return;

    float base_val;
    pthread_mutex_lock(&s->lock);
    base_val = s->value;
    pthread_mutex_unlock(&s->lock);

    float smoothed_base =
        process_smoother(&s->smooth_val, base_val);

    if (smoothed_base < -1.0f) smoothed_base = -1.0f;
    if (smoothed_base >  1.0f) smoothed_base =  1.0f;

    const float* cv = NULL;
    for (int i = 0; i < m->num_control_inputs; i++)
    {
        if (!m->control_inputs[i] || !m->control_input_params[i])
            continue;
        if (strcmp(m->control_input_params[i], "val") == 0)
        {
            cv = m->control_inputs[i];
            break;
        }
    }

    memset(outR, 0, frames * sizeof(float));  // mono only

    float last_value = smoothed_base;

    for (unsigned long i = 0; i < frames; i++) {
        float v = smoothed_base;

        // CV affects output directly (NOT smoothed)
        if (cv)
            v += cv[i];

        // Final clamp
        if (v < -1.0f) v = -1.0f;
        if (v >  1.0f) v =  1.0f;

        outL[i] = v;
        last_value = v;
    }

    pthread_mutex_lock(&s->lock);
    s->display_value = last_value;
    pthread_mutex_unlock(&s->lock);
}

static void c_output_draw_ui(Module* m, int y, int x) {
    COutputState* s = (COutputState*)m->state;
    pthread_mutex_lock(&s->lock);
    float val = s->display_value;
    pthread_mutex_unlock(&s->lock);

	BLUE();
    mvprintw(y,   x, "[c_output:%s] ", m->name);
	CLR();

	LABEL(2, "val:");
	ORANGE(); printw(" %.3f", val); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= adjust val");
    mvprintw(y+2, x, "Command mode: :1 [val]");
	BLACK();
}

static void c_output_handle_input(Module* m, int key) {
    COutputState* s = (COutputState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);
    if (!s->entering_command) {
        switch (key) {
            case '-': s->value -= 0.01f; handled = 1; break;
            case '=': s->value += 0.01f; handled = 1; break;
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
            char type; float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2 && type == '1')
                s->value = val;
            handled = 1;
        } else if (key == 27) { // ESC
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

    if (handled) {
        if (s->value < -1.0f) s->value = -1.0f;
        if (s->value >  1.0f) s->value =  1.0f;
    }
    pthread_mutex_unlock(&s->lock);
}

static void c_output_destroy(Module* m) {
    COutputState* s = (COutputState*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float val = 0.0f;
	int ch = -1;

    if (args && strstr(args, "val="))
        sscanf(strstr(args, "val="), "val=%f", &val);

	if (args && strstr(args, "ch="))
		sscanf(strstr(args, "ch="), "ch=%d", &ch);

    COutputState* s = calloc(1, sizeof(COutputState));
    s->value = val;
	s->target_channel = ch;
    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_val, 0.5f);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_output";
	m->type = "c_output";
    m->state = s;
    m->process = c_output_process;
    m->draw_ui = c_output_draw_ui;
    m->handle_input = c_output_handle_input;
    m->destroy = c_output_destroy;

    // AUDIO output (for DC-coupled DAC)
    m->output_bufferL = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->output_bufferR = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->output_buffer = m->output_bufferL;
    return m;
}

