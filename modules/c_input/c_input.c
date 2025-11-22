#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "module.h"
#include "util.h"
#include "c_input.h"

static void c_input_process(Module* m, float* in, unsigned long frames)
{
    CInputState* s = (CInputState*)m->state;
    float* out = m->control_output;

    pthread_mutex_lock(&s->lock);
    pthread_mutex_unlock(&s->lock);

    for (unsigned long i = 0; i < frames; i++) {
        float v = in ? in[i] : 0.0f;

        // smooth it to control-rate output
        v = process_smoother(&s->smooth, v);
        out[i] = v;
        s->last_val = v;
    }
}

static void c_input_draw_ui(Module* m, int y, int x)
{
    CInputState* s = (CInputState*)m->state;
    pthread_mutex_lock(&s->lock);
    float v = s->last_val;
    int ch = s->channel_index;
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[c_input:%s] ", m->name);
    CLR();

    LABEL(2, "ch:");
    ORANGE(); printw(" %d | ", ch); CLR();

    LABEL(2, "val:");
    ORANGE(); printw(" %.3f", v); CLR();

    YELLOW();
    mvprintw(y+1, x, "Command mode: :1 [channel]");
    BLACK();
}

static void c_input_handle_input(Module* m, int key)
{
    CInputState* s = (CInputState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        if (key == ':') {
            s->entering_command = true;
            memset(s->command_buffer, 0, sizeof(s->command_buffer));
            s->command_index = 0;
            handled = 1;
        }
    } else {
        if (key == '\n') {
            s->entering_command = false;
            char type; int ch;
            if (sscanf(s->command_buffer, "%c %d", &type, &ch) == 2 && type == '1')
                s->channel_index = ch;
            handled = 1;
        } else if (key == 27) { // ESC
            s->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 && s->command_index < sizeof(s->command_buffer)-1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }
	
	if (handled) {
		// Do something?
	}

    pthread_mutex_unlock(&s->lock);
}

static void c_input_set_osc_param(Module* m, const char* param, float value)
{
    CInputState* s = (CInputState*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "ch") == 0)
        s->channel_index = (int)value;
    else
        fprintf(stderr, "[c_input] Unknown param: %s\n", param);

    pthread_mutex_unlock(&s->lock);
}

static void c_input_destroy(Module* m)
{
    CInputState* s = (CInputState*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate)
{
    int ch = 1;

    if (args && strstr(args, "ch="))
        sscanf(strstr(args, "ch="), "ch=%d", &ch);

    CInputState* s = calloc(1, sizeof(CInputState));
    s->sample_rate = sample_rate;
    s->channel_index = ch;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth, 0.3f);
    s->last_val = 0.0f;

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_input";
    m->type = "c_input";
    m->state = s;

    m->process = c_input_process;
    m->draw_ui = c_input_draw_ui;
    m->handle_input = c_input_handle_input;
    m->set_param = c_input_set_osc_param;
    m->destroy = c_input_destroy;

    // CONTROL output buffer
    m->control_output = calloc(FRAMES_PER_BUFFER, sizeof(float));

    return m;
}

