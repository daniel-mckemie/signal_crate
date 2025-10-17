#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <ncurses.h>
#include <lo/lo.h>   // liblo (already used by your osc.c)
#include "scriptbox.h"
#include "module.h"
#include "util.h"

static void script_box_process_control(Module* m) {
    // no continuous output — only dispatches commands
}

static void script_box_draw_ui(Module* m, int y, int x) {
    ScriptBox* s = (ScriptBox*)m->state;
    pthread_mutex_lock(&s->lock);
    mvprintw(y,   x, "[Script:%s] :rand(min,max,alias,param) or :set(val,alias,param)", m->name);
    mvprintw(y+1, x, "Cmd: %s", s->command_buffer);
    mvprintw(y+2, x, "Result: %s", s->last_result);
    pthread_mutex_unlock(&s->lock);
}

/* Dispatch OSC to local server */
static void send_osc(const char* alias, const char* param, float value) {
	const char* port = getenv("SIGNAL_CRATE_OSC_PORT"); // Get OSC port from osc.h
	if (!port || !*port) return; // OSC not started yet

    lo_address t = lo_address_new("127.0.0.1", port);  // same port your OSC server uses
	if (!t) return;
	
    char path[128];
    snprintf(path, sizeof(path), "/%s/%s", alias, param);
    lo_send(t, path, "f", value);
    lo_address_free(t);
}

/* Interpret and run commands */
static void run_script_command(ScriptBox* s, const char* cmd) {
    char func[32], alias[64], param[64];
    float a = 0.f, b = 0.f;
    int n = sscanf(cmd, "%31[^ (](%f,%f,%63[^,],%63[^)])", func, &a, &b, alias, param);

    if (n < 4) {
        snprintf(s->last_result, sizeof(s->last_result), "Parse error");
        return;
    }

    float val = 0.f;
    if (strncmp(func, "rand", 4) == 0 && n == 5)
        val = a + randf() * (b - a);
    else if (strncmp(func, "set", 3) == 0 && n >= 4)
        val = a;
    else {
        snprintf(s->last_result, sizeof(s->last_result), "Bad func: %s", func);
        return;
    }
	
	// Convert to normalized 0–1 range
	float scaled = val;

	// Apply logarithmic scaling for frequency-like parameters
	if (strcmp(param, "freq") == 0 || strcmp(param, "frequency") == 0) {
		// Convert Hz to 0–1 control (inverse of VCO mapping)
		const float fmin = 20.0f;
		const float fmax = 20000.0f; // or whatever your engine’s top range is
		if (val < fmin) val = fmin;
		if (val > fmax) val = fmax;
		scaled = logf(val / fmin) / logf(fmax / fmin);
	}
	else {
		// Linear normalization between min and max
		if (b > a)
			scaled = (val - a) / (b - a);
		else
			scaled = val;
	}

	// Clamp safety
	if (scaled < 0.f) scaled = 0.f;
	if (scaled > 1.f) scaled = 1.f;

	send_osc(alias, param, scaled);
	snprintf(s->last_result, sizeof(s->last_result),
			 "sent %s/%s=%.2f (raw %.2f)", alias, param, scaled, val);
}
		

/* ncurses input handling */
static void script_box_handle_input(Module* m, int key) {
    ScriptBox* s = (ScriptBox*)m->state;
    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        if (key == ':') {
            s->entering_command = 1;
            s->command_index = 0;
            memset(s->command_buffer, 0, sizeof(s->command_buffer));
        }
    } else {
        if (key == '\n') {
            s->entering_command = 0;
            run_script_command(s, s->command_buffer);
        } else if (key == 27) {
            s->entering_command = 0;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_buffer[--s->command_index] = '\0';
        } else if (key >= 32 && key < 127 && s->command_index < (int)sizeof(s->command_buffer)-1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
        }
    }

    pthread_mutex_unlock(&s->lock);
}

static void script_box_destroy(Module* m) {
    ScriptBox* s = (ScriptBox*)m->state;
    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    (void)args; (void)sample_rate;
    ScriptBox* s = calloc(1, sizeof(ScriptBox));
    pthread_mutex_init(&s->lock, NULL);

    Module* m = calloc(1, sizeof(Module));
    m->name = "script_box";
    m->state = s;
    m->draw_ui = script_box_draw_ui;
    m->handle_input = script_box_handle_input;
    m->process_control = script_box_process_control;
    m->destroy = script_box_destroy;
    return m;
}

