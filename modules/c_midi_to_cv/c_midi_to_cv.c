#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>

#include "module.h"
#include "util.h"
#include "midi.h"
#include "c_midi_to_cv.h"

static void c_midi_to_cv_process(Module* m, float* in, unsigned long frames) {
    (void)in;
    CMidiToCVState* s = (CMidiToCVState*)m->state;
    float* out = m->control_output;

    float last = s->last_val;

	float v = 0.0f;
	int last_chan = midi_last_channel();

	if (s->chan == 0 || s->chan == last_chan) {
		if (s->cc < 32) {
			v = midi_cc14_norm(s->cc);   // uses CC + CC+32
		} else {
			v = midi_cc_norm(s->cc);     // legacy 7-bit
		}
	}

    for (unsigned long i = 0; i < frames; i++) {
        float sm = process_smoother(&s->smooth, v);
        out[i] = sm;
        last = sm;
    }

    pthread_mutex_lock(&s->lock);
    s->last_val = last;
    pthread_mutex_unlock(&s->lock);
}

static void c_midi_to_cv_draw_ui(Module* m, int y, int x) {
    CMidiToCVState* s = (CMidiToCVState*)m->state;

    pthread_mutex_lock(&s->lock);
    float v = s->last_val;
    int cc = s->cc;
	int chan = s->chan;
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[c_midi_to_cv:%s] ", m->name);
    CLR();
	
    LABEL(2, "chan:");
    ORANGE(); printw(" %d", chan); CLR();

    LABEL(2, "cc:");
    ORANGE(); printw(" %d", cc); CLR();

    LABEL(2, "val:");
    ORANGE(); printw(" %.3f", v); CLR();

    YELLOW();
    mvprintw(y+1, x, "Command mode: :1 [chan#] :2 [cc#]");
    BLACK();
}

static void c_midi_to_cv_handle_input(Module* m, int key) {
    CMidiToCVState* s = (CMidiToCVState*)m->state;
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
            char type; int chan; int cc;
            if (sscanf(s->command_buffer, "%c %d", &type, &chan) == 2 && type == '1') {
                if (chan < 0) chan = 0;
                if (chan > 16) chan = 16;
                s->chan = chan;
            }
            if (sscanf(s->command_buffer, "%c %d", &type, &cc) == 2 && type == '2') {
                if (cc < 0) cc = 0;
                if (cc > 127) cc = 127;
                s->cc = cc;
            }
            handled = 1;
        } else if (key == 27) { // ESC
            s->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 && s->command_index < (int)sizeof(s->command_buffer)-1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    (void)handled;
    pthread_mutex_unlock(&s->lock);
}

static void c_midi_to_cv_destroy(Module* m) {
    CMidiToCVState* s = (CMidiToCVState*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);

    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	int cc   = 1;
	int chan = 0;  // 0 = any channel

	if (args) {
		if (strstr(args, "cc="))
			sscanf(strstr(args, "cc="), "cc=%d", &cc);
		if (strstr(args, "chan="))
			sscanf(strstr(args, "chan="), "chan=%d", &chan);
	}

	if (cc < 0) cc = 0;
	if (cc > 127) cc = 127;
	if (chan < 0) chan = 0;
	if (chan > 16) chan = 16;

    CMidiToCVState* s = calloc(1, sizeof(CMidiToCVState));
    s->sample_rate = sample_rate;
    s->cc = cc;
	s->chan = chan;
    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth, 0.15f);
    s->last_val = 0.0f;

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_midi_to_cv";
    m->type = "c_midi_to_cv";
    m->state = s;

    m->process = c_midi_to_cv_process;
    m->draw_ui = c_midi_to_cv_draw_ui;
    m->handle_input = c_midi_to_cv_handle_input;
    m->destroy = c_midi_to_cv_destroy;

    m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
    return m;
}

