#include <ncurses.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "module.h"
#include "engine.h"
#include "osc.h"
#include "midi.h"

#define COLUMN_WIDTH 72
#define TRUNC_WIDTH 48

static struct timespec last_time = {0};
static struct rusage last_usage = {0};

int truncated = 1;

float get_cpu_percent() {
	struct rusage usage_now;
	struct timespec time_now;

	getrusage(RUSAGE_SELF, &usage_now);
	clock_gettime(CLOCK_MONOTONIC, &time_now);

	double delta_user = (usage_now.ru_utime.tv_sec - last_usage.ru_utime.tv_sec) +
	                    (usage_now.ru_utime.tv_usec - last_usage.ru_utime.tv_usec) / 1e6;
	double delta_sys = (usage_now.ru_stime.tv_sec - last_usage.ru_stime.tv_sec) +
	                   (usage_now.ru_stime.tv_usec - last_usage.ru_stime.tv_usec) / 1e6;
	double delta_cpu = delta_user + delta_sys;

	double delta_time = (time_now.tv_sec - last_time.tv_sec) +
	                    (time_now.tv_nsec - last_time.tv_nsec) / 1e9;

	last_usage = usage_now;
	last_time = time_now;

	if (delta_time <= 0.0) return 0.0f;

	return (float)(100.0 * delta_cpu / delta_time);
}

void ui_loop() {
	if (!ui_enabled) {
		fprintf(stderr, "[ui] Skipping UI (disabled by patch flag)\n");
        while (1) usleep(100000);  // keep audio thread alive
        return;
	}

    initscr(); set_escdelay(25); cbreak(); noecho();
	start_color();
	use_default_colors();   // lets foreground go transparent

	init_pair(1, COLOR_CYAN, -1);   // light blue-ish
	init_pair(2, COLOR_GREEN, -1);  // green
	init_pair(3, COLOR_YELLOW, -1); // yellow
	init_color(208, 1000, 498, 0);   // (R,G,B from 0–1000)
	init_pair(4, 208, -1);           // foreground = orange
	init_pair(5, COLOR_BLACK, -1);  // black


	scrollok(stdscr, FALSE);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

	int cpu_refresh_counter = 0;
	float cpu = 0.0f;

    int focused_module_index = 0;
    int in_command_mode = 0;
    char command[128] = "";
    int cmd_index = 0;
    int running = 1;

	int mod_x[MAX_MODULES] = {0};
	int mod_y[MAX_MODULES] = {0};

    while (running) {
        erase();
		attrset(A_NORMAL);
        mvprintw(0, 2, "--- Signal Crate ---");

		// CPU Usage
		cpu_refresh_counter++;
		if (cpu_refresh_counter >= 20) {
			cpu = get_cpu_percent();
			cpu_refresh_counter = 0;
		}

		// Show CPU and OSC port
		const char* osc_port = get_current_osc_port();
		int mchan = midi_last_channel();
		int mcc   = midi_last_cc();

		if (mchan > 0 && mcc >= 0)
			mvprintw(0, COLS - 48, "[CPU] %.1f%%  [OSC:%s]  [MIDI ch:%d cc:%d]",
					 cpu, osc_port, mchan, mcc);
		else
			mvprintw(0, COLS - 30, "[CPU] %.1f%%  [OSC:%s]", cpu, osc_port);


		int rows, cols;
		getmaxyx(stdscr, rows, cols);

        int base_module_height = 3;
		int module_spacing = 1;
        int module_count = get_module_count();
		int modules_per_col = (rows - 4) / (base_module_height + module_spacing);
		if (modules_per_col < 1) modules_per_col = 1;

		int col_heights[64] = {0};

        for (int i = 0; i < module_count; i++) {
            Module* m = get_module(i);
            if (m && m->draw_ui) {
				int col = i / modules_per_col;
				int module_height = truncated ? 1 : base_module_height;
				
				int y = 2 + col_heights[col];
				int x = 2 + col * COLUMN_WIDTH;

				mod_x[i] = x;
				mod_y[i] = y;

                if (i == focused_module_index)
                    attron(A_REVERSE);
				move(y, x);
                m->draw_ui(m, y, x);

				attrset(A_NORMAL);
				color_set(0, NULL);

				if (truncated) {
					for (int k = 1; k < 3; k++)
						mvhline(y + k, x, ' ', COLUMN_WIDTH);
				}

                if (i == focused_module_index)
                    attroff(A_REVERSE);

				col_heights[col] += module_height + module_spacing;
            }
        }

        if (in_command_mode) {
			attrset(A_NORMAL);
            mvprintw(LINES - 2, 2, ": %s", command);
        } else {
			attrset(A_NORMAL);
            mvprintw(LINES - 2, 2, "[TAB] switch module | [t] show/hide cmds | [:q] quit | [:] cmd mode | [ESCx2] exit cmd mode");
        }

        refresh();
        int ch = getch();

        if (ch == ERR) {
            napms(10);
            continue;
        }

        Module* focused = get_module(focused_module_index);

        if (in_command_mode) {
            if (ch == '\n') {
                command[cmd_index] = '\0';

                if (strcmp(command, "q") == 0) {
                    running = 0;
                    break;
                }

                if (focused && focused->handle_input) {
                    for (int j = 0; j < (int)strlen(command); j++) {
                        focused->handle_input(focused, command[j]);
                    }
                    focused->handle_input(focused, '\n');
                }

                cmd_index = 0;
                command[0] = '\0';
                in_command_mode = 0;
            } else if (ch == 27) {
                cmd_index = 0;
                command[0] = '\0';
                in_command_mode = 0;
            } else if ((ch == KEY_BACKSPACE || ch == 127) && cmd_index > 0) {
                cmd_index--;
                command[cmd_index] = '\0';
            } else if (ch >= 32 && ch < 127 && cmd_index < sizeof(command) - 1) {
                command[cmd_index++] = (char)ch;
                command[cmd_index] = '\0';
            }
        } else {
            if (ch == '\t') {
                focused_module_index = (focused_module_index + 1) % module_count;
            } else if (ch == KEY_UP || ch == KEY_DOWN || ch == KEY_LEFT || ch == KEY_RIGHT) {
				int best_index = focused_module_index;
				int best_distance = 99999;

				int fx = mod_x[focused_module_index];
				int fy = mod_y[focused_module_index];

				for (int i = 0; i < module_count; i++) {
					if (i == focused_module_index) continue;

					int dx = mod_x[i] - fx;
					int dy = mod_y[i] - fy;

					if (ch == KEY_UP && dy < 0 && abs(dx) < COLUMN_WIDTH / 2) {
						if (-dy < best_distance) {
							best_distance = -dy;
							best_index = i;
						}
					} else if (ch == KEY_DOWN && dy > 0 && abs(dx) < COLUMN_WIDTH / 2) {
						if (dy < best_distance) {
							best_distance = dy;
							best_index = i;
						}
					} else if (ch == KEY_LEFT && dx < 0 && abs(dy) < 3) {
						if (-dx < best_distance) {
							best_distance = -dx;
							best_index = i;
						}
					} else if (ch == KEY_RIGHT && dx > 0 && abs(dy) < 3) {
						if (dx < best_distance) {
							best_distance = dx;
							best_index = i;
						}
					}
				}
				focused_module_index = best_index;
            } else if (ch == ':') {
                in_command_mode = 1;
                cmd_index = 0;
                command[0] = '\0';
                if (focused && focused->handle_input)
                    focused->handle_input(focused, ch);
			} else if (ch == 't') {
				truncated = !truncated;
            } else {
                if (focused && focused->handle_input)
                    focused->handle_input(focused, ch);
            }
        }
    }

    endwin();
}

