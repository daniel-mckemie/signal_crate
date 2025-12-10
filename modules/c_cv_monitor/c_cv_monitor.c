#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_cv_monitor.h"
#include "module.h"
#include "util.h"

static void cv_monitor_process_control(Module* m) {
    CCVMonitor* s = (CCVMonitor*)m->state;

    // 1. Read UI params
    float att_base, off_base;
    pthread_mutex_lock(&s->lock);
    att_base = s->attenuvert;
    off_base = s->offset;
    pthread_mutex_unlock(&s->lock);

    // 2. Smooth ONLY UI/OSC params
    float att = process_smoother(&s->smooth_att, att_base);
    float off = process_smoother(&s->smooth_off, off_base);

    // 3. Apply CV modulation immediately (no smoothing)
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
        float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "att") == 0) {
            float mod_range = (2.0f - fabsf(att_base));
            att = att + norm * mod_range;   // NO smoothing here

        } else if (strcmp(param, "offset") == 0) {
            float mod_range = (1.0f - fabsf(off_base));
            off = off + norm * mod_range;   // NO smoothing here
        }
    }

    // 4. CV in
    float in = m->control_inputs[0] ? *(m->control_inputs[0]) : 0.0f;
    float out = fminf(fmaxf(in * att + off, -1.0f), 1.0f);

    // 5. Write state for UI
    pthread_mutex_lock(&s->lock);
    s->input = in;
    s->output = out;
    s->display_input  = in;
    s->display_output = out;
    s->display_att    = att;
    s->display_off    = off;
    pthread_mutex_unlock(&s->lock);

    // 6. Output buffer
    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++)
        m->control_output[i] = out;
}


static void clamp_params(CCVMonitor* s) {
    clampf(&s->attenuvert, -2.0f, 2.0f);
    clampf(&s->offset, -1.0f, 1.0f);
}

static void cv_monitor_draw_ui(Module* m, int y, int x) {
    CCVMonitor* s = (CCVMonitor*)m->state;
    pthread_mutex_lock(&s->lock);
    float in = s->display_input;
    float out = s->display_output;
    float att = s->display_att;
    float off = s->display_off;
    pthread_mutex_unlock(&s->lock);

	BLUE();
	mvprintw(y,   x, "[CVMon:%s] ", m->name);
	CLR();

	LABEL(2, "in:");
	ORANGE(); printw(" %.3f | ", in); CLR();

	LABEL(2, "att:");
	ORANGE(); printw(" %.2f | ", att); CLR();
	
	LABEL(2, "off:");
	ORANGE(); printw(" %.2f | ", off); CLR();

	LABEL(2, "out:");
	ORANGE(); printw(" %.3f", out); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-Time Keys: -/= att, _/+ offset");
    mvprintw(y+2, x, "Cmd Keys: :1 att, :2 offset");
	BLACK();
}

static void cv_monitor_handle_input(Module* m, int key) {
    CCVMonitor* s = (CCVMonitor*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);
    if (!s->entering_command) {
        switch (key) {
            case '=': s->attenuvert += 0.01f; handled = 1; break;
            case '-': s->attenuvert -= 0.01f; handled = 1; break;
            case '+': s->offset += 0.01f; handled = 1; break;
            case '_': s->offset -= 0.01f; handled = 1; break;
            case ':':
                s->entering_command = true;
                s->command_index = 0;
                memset(s->command_buffer, 0, sizeof(s->command_buffer));
                handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            s->entering_command = false;
            char type;
            float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') s->attenuvert = val;
                else if (type == '2') s->offset = val;
            }
            handled = 1;
        } else if (key == 27) {
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

    if (handled)
        clamp_params(s);

    pthread_mutex_unlock(&s->lock);
}

static void cv_monitor_set_osc_param(Module* m, const char* param, float value) {
    CCVMonitor* s = (CCVMonitor*)m->state;
    pthread_mutex_lock(&s->lock);
    if (strcmp(param, "att") == 0)
        s->attenuvert = fminf(fmaxf(value, -2.0f), 2.0f);
    else if (strcmp(param, "offset") == 0)
        s->offset = fminf(fmaxf(value, -1.0f), 1.0f);
    else
        fprintf(stderr, "[c_cv_monitor] Unknown param: %s\n", param);
    pthread_mutex_unlock(&s->lock);
}

static void cv_monitor_destroy(Module* m) {
    CCVMonitor* s = (CCVMonitor*)m->state;
    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float att = 1.0f;
    float off = 0.0f;

    if (args && strstr(args, "att=")) sscanf(strstr(args, "att="), "att=%f", &att);
    if (args && strstr(args, "offset=")) sscanf(strstr(args, "offset="), "offset=%f", &off);

    CCVMonitor* s = calloc(1, sizeof(CCVMonitor));
    s->attenuvert = att;
    s->offset = off;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_att, 0.75f);
    init_smoother(&s->smooth_off, 0.75f);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_cv_monitor";
    m->state = s;
    m->process_control = cv_monitor_process_control;
    m->draw_ui = cv_monitor_draw_ui;
    m->handle_input = cv_monitor_handle_input;
	m->set_param = cv_monitor_set_osc_param;
    m->destroy = cv_monitor_destroy;
    m->control_output = calloc(FRAMES_PER_BUFFER, sizeof(float));
    return m;
}

