#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "module.h"
#include "util.h"
#include "c_output.h"

static void c_output_process(Module* m, float* in, unsigned long frames) {
    COutputState* state = (COutputState*)m->state;
    float base_val;
	float* outL = m->output_bufferL;
	float* outR = m->output_bufferR;

	if (!outL || !outR) return;

    pthread_mutex_lock(&state->lock);
    base_val = state->value;
    pthread_mutex_unlock(&state->lock);

    // Check for a control source (mod input)
    const float* mod_src = NULL;
    for (int i = 0; i < m->num_control_inputs; i++) {
        if (!m->control_inputs[i] || !m->control_input_params[i]) continue;
        const char* param = m->control_input_params[i];
        if (strcmp(param, "val") == 0) {
            mod_src = m->control_inputs[i];
            break;
        }
    }

    float last_value = base_val;

	// Stereo established to work with engine.c
	memset(outR, 0, frames * sizeof(float)); // Only want mono
    for (unsigned long i = 0; i < frames; i++) {
        float value = base_val;

        // If a control source exists, use its sample + base offset
        if (mod_src)
            value += mod_src[i];

        // Apply smoothing and clamp
        value = process_smoother(&state->smooth_val, value);
        if (value < -1.0f) value = -1.0f;
        if (value >  1.0f) value =  1.0f;
        outL[i] = value;
        last_value = value;
    }

    // Update display with last processed value
    state->display_value = last_value;
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
        s->display_value = s->value;
    }
    pthread_mutex_unlock(&s->lock);
}

// UNCOMMENT IF THIS IS NOT WORKING WITH NEW SETUP
/*
static void c_output_set_osc_param(Module* m, const char* param, float value) {
    COutputState* s = (COutputState*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "val") == 0)
        s->value = fminf(fmaxf(value, -1.0f), 1.0f);
    else
        fprintf(stderr, "[c_output] Unknown OSC param: %s\n", param);

    pthread_mutex_unlock(&s->lock);
}
*/

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
// UNCOMMENT IF THIS IS NOT WORKING WITH NEW SETUP
    // m->set_param = c_output_set_osc_param;
    m->destroy = c_output_destroy;

    // AUDIO output (for DC-coupled DAC)
    m->output_bufferL = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->output_bufferR = calloc(FRAMES_PER_BUFFER, sizeof(float));
	m->output_buffer = m->output_bufferL;
    return m;
}

