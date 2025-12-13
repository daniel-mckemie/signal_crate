#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <ncurses.h>
#include <lo/lo.h>

#include "scriptbox.h"
#include "module.h"
#include "util.h"
#include "scheduler.h"

static void run_script_command(ScriptBox* s, const char* cmd);
// Callback implementation — re-runs the script line
void run_script_line_cb(void* userdata) {
    ScriptEvent* e = (ScriptEvent*)userdata;
    ScriptBox* s = (ScriptBox*)e->scriptbox_ptr;
    run_script_command(s, e->script_line);
}

static void script_box_process_control(Module* m, unsigned long frames) {
    // no continuous output — only dispatches commands
}

static void script_box_draw_ui(Module* m, int y, int x) {
    ScriptBox* s = (ScriptBox*)m->state;
    pthread_mutex_lock(&s->lock);

    mvprintw(y, x, "[Script:%s] (Enter edit | ESC exit | Ctrl-R run)", m->name);

    // Compute cursor line/column
    int cur_line = 0, cur_col = 0;
    for (int i = 0; i < s->cursor_pos && i < (int)sizeof(s->script_text); i++) {
        if (s->script_text[i] == '\n') {
            cur_line++;
            cur_col = 0;
        } else cur_col++;
    }

    // Draw up to 6 lines of text
    int max_lines = 6;
    int idx = 0;
    for (int l = 0; l < max_lines && s->script_text[idx]; l++) {
        char linebuf[128];
        int len = 0;
        while (s->script_text[idx] && s->script_text[idx] != '\n' && len < 127) {
            linebuf[len++] = s->script_text[idx++];
        }
        linebuf[len] = '\0';
        if (s->script_text[idx] == '\n') idx++;
        mvprintw(y + 1 + l, x, "%-70s", linebuf);
    }

    mvprintw(y + 8, x, "Result: %s", s->last_result);

    // --- Move and show cursor only if editing ---
    if (s->editing) {
        int cy = y + 1 + cur_line;
        int cx = x + cur_col;
        move(cy, cx);
        curs_set(1);          // show blinking cursor
    } else {
        curs_set(0);          // hide cursor when not editing
    }

    pthread_mutex_unlock(&s->lock);
}

static void send_osc(const char* alias, const char* param, float value) {
    const char* port = getenv("SIGNAL_CRATE_OSC_PORT");
    if (!port || !*port) return;
    lo_address t = lo_address_new("127.0.0.1", port);
    if (!t) return;

    char path[128];
    snprintf(path, sizeof(path), "/%s/%s", alias, param);
    lo_send(t, path, "f", value);
    lo_address_free(t);
}

static void run_script_command(ScriptBox* s, const char* cmd) {
    char func[32], alias[64], param[64], fifth[64] = {0};
    float a = 0.f, b = 0.f;

    // Try parsing up to 5 args (optional fifth may be "~interval")
    int n = sscanf(cmd, "%31[^ (](%f,%f,%63[^,],%63[^,],%63[^)])",
                   func, &a, &b, alias, param, fifth);

    if (n < 4) {
        snprintf(s->last_result, sizeof(s->last_result), "Parse error: %s", cmd);
        return;
    }

    // --- Strip any trailing ) or whitespace from alias/param ---
    for (int i = 0; alias[i]; i++) {
        if (alias[i] == ')' || alias[i] == '\n' || alias[i] == '\r')
            alias[i] = '\0';
    }
    for (int i = 0; param[i]; i++) {
        if (param[i] == ')' || param[i] == '\n' || param[i] == '\r')
            param[i] = '\0';
    }

    // --- Detect optional ~interval ---
    double interval_ms = 0.0;
    if ((n == 6 || n == 5) && fifth[0] == '~')
        interval_ms = atof(fifth + 1);

    // --- Compute value ---
    float val = 0.f;
    if (strncmp(func, "rand", 4) == 0)
        val = a + randf() * (b - a);
    else if (strncmp(func, "set", 3) == 0)
        val = a;
    else {
        snprintf(s->last_result, sizeof(s->last_result), "Bad func: %s", func);
        return;
    }

    // --- Normalize ---
    float scaled = val;
    if (strcmp(param, "freq") == 0 || strcmp(param, "cutoff") == 0) {
        float fmin = 20.0f, fmax = 20000.0f;
        if (val < fmin) val = fmin;
        if (val > fmax) val = fmax;
        scaled = logf(val / fmin) / logf(fmax / fmin);
    } else if (b > a) {
        scaled = (val - a) / (b - a);
    }

    if (scaled < 0.f) scaled = 0.f;
    if (scaled > 1.f) scaled = 1.f;

    // --- Send OSC to correct alias/param ---
    send_osc(alias, param, scaled);
    // snprintf(s->last_result, sizeof(s->last_result),
    //         "sent %s/%s=%.2f (raw %.2f)", alias, param, scaled, val);

    // --- If ~interval provided, schedule repeats ---
    if (interval_ms > 0.0) {
        ScriptEvent* e = malloc(sizeof(ScriptEvent));
        snprintf(e->script_line, sizeof(e->script_line), "%s", cmd);
        e->scriptbox_ptr = s;
        scheduler_add(run_script_line_cb, e, interval_ms);
        // fprintf(stderr, "[scheduler] '%s' scheduled every %.1f ms\n",
        //        cmd, interval_ms);
    }
}


static void script_box_handle_input(Module* m, int key) {
    ScriptBox* s = (ScriptBox*)m->state;

    // --- ENTER toggles into edit mode ---
    if (!s->editing && key == '\n') {
        s->editing = 1;
        keypad(stdscr, TRUE);
        nodelay(stdscr, FALSE);
        curs_set(1);

        int ch;
        while (s->editing) {
            pthread_mutex_lock(&s->lock);

            // Redraw current text
            erase();
            mvprintw(0, 0, "[ScriptBox] (ESC to exit)");
            int cur_line = 0, cur_col = 0;
            for (int i = 0; i < s->cursor_pos; i++) {
                if (s->script_text[i] == '\n') {
                    cur_line++;
                    cur_col = 0;
                } else cur_col++;
            }

            // Display script text
            int max_lines = 10;
            int idx = 0;
            for (int l = 0; l < max_lines && s->script_text[idx]; l++) {
                char linebuf[128];
                int len = 0;
                while (s->script_text[idx] && s->script_text[idx] != '\n' && len < 127)
                    linebuf[len++] = s->script_text[idx++];
                linebuf[len] = '\0';
                if (s->script_text[idx] == '\n') idx++;
                mvprintw(2 + l, 2, "%-70s", linebuf);
            }

            // Position cursor
            move(2 + cur_line, 2 + cur_col);
            refresh();
            pthread_mutex_unlock(&s->lock);

            ch = getch();

            pthread_mutex_lock(&s->lock);
            switch (ch) {
                case 27: // ESC exits
                    s->editing = 0;
                    break;
                case KEY_BACKSPACE:
                case 127:
                    if (s->cursor_pos > 0) {
                        memmove(&s->script_text[s->cursor_pos - 1],
                                &s->script_text[s->cursor_pos],
                                strlen(&s->script_text[s->cursor_pos]) + 1);
                        s->cursor_pos--;
                    }
                    break;
                case '\n':
                    memmove(&s->script_text[s->cursor_pos + 1],
                            &s->script_text[s->cursor_pos],
                            strlen(&s->script_text[s->cursor_pos]) + 1);
                    s->script_text[s->cursor_pos++] = '\n';
                    break;
                case KEY_LEFT:
                    if (s->cursor_pos > 0) s->cursor_pos--;
                    break;
                case KEY_RIGHT:
                    if (s->cursor_pos < (int)strlen(s->script_text)) s->cursor_pos++;
                    break;
                case KEY_UP:
                case KEY_DOWN: {
                    int cur_line2 = 0, cur_col2 = 0;
                    for (int i = 0; i < s->cursor_pos; i++) {
                        if (s->script_text[i] == '\n') {
                            cur_line2++;
                            cur_col2 = 0;
                        } else cur_col2++;
                    }
                    int target_line = cur_line2 + (ch == KEY_DOWN ? 1 : -1);
                    if (target_line < 0) target_line = 0;
                    int i = 0, l = 0, c = 0, target_pos = s->cursor_pos;
                    while (s->script_text[i]) {
                        if (l == target_line && c == cur_col2) { target_pos = i; break; }
                        if (s->script_text[i] == '\n') { l++; c = 0; }
                        else c++;
                        i++;
                    }
                    s->cursor_pos = target_pos;
                    break;
                }
                default:
                    if (ch >= 32 && ch < 127 && s->cursor_pos < (int)sizeof(s->script_text) - 1) {
                        memmove(&s->script_text[s->cursor_pos + 1],
                                &s->script_text[s->cursor_pos],
                                strlen(&s->script_text[s->cursor_pos]) + 1);
                        s->script_text[s->cursor_pos++] = (char)ch;
                    }
                    break;
            }
            pthread_mutex_unlock(&s->lock);
        }

        curs_set(0);
        nodelay(stdscr, TRUE);
        return;
    }

    // --- Ctrl+R (execute) still works outside edit mode ---
    if (!s->editing && key == 18) {
        pthread_mutex_lock(&s->lock);
        char copy[2048];
        strncpy(copy, s->script_text, sizeof(copy));
        pthread_mutex_unlock(&s->lock);

		char* line = strtok(copy, "\n");
		while (line) {
			// Trim leading spaces
			while (*line == ' ' || *line == '\t') line++;

			// Skip comment lines and empty lines
			if (strncmp(line, "//", 2) != 0 && *line != '\0') {
				run_script_command(s, line);
			}

			line = strtok(NULL, "\n");
		}

        pthread_mutex_lock(&s->lock);
        snprintf(s->last_result, sizeof(s->last_result), "script executed");
        pthread_mutex_unlock(&s->lock);
    }
}

static void script_box_destroy(Module* m) {
    ScriptBox* s = (ScriptBox*)m->state;
    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    (void)args;
    ScriptBox* s = calloc(1, sizeof(ScriptBox));
    s->sample_rate = sample_rate;
    pthread_mutex_init(&s->lock, NULL);

    Module* m = calloc(1, sizeof(Module));
    m->name = "scriptbox";
    m->state = s;
    m->draw_ui = script_box_draw_ui;
    m->handle_input = script_box_handle_input;
    m->process_control = script_box_process_control;
    m->destroy = script_box_destroy;

    keypad(stdscr, TRUE); // ensure arrow keys are recognized
	nodelay(stdscr, FALSE);
    curs_set(1); // enable blinking cursor globally

    return m;
}

