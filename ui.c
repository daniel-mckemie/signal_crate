#include <ncurses.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>

#include "module.h"
#include "engine.h"
#include "osc.h"

#define COLUMN_WIDTH 64

static struct timespec last_time = {0};
static struct rusage last_usage = {0};

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
    initscr(); cbreak(); noecho();
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

    while (running) {
        clear();
        mvprintw(0, 2, "--- Signal Crate ---");

		// CPU Usage
		cpu_refresh_counter++;
		if (cpu_refresh_counter >= 20) {
			cpu = get_cpu_percent();
			cpu_refresh_counter = 0;
		}

		// Show CPU and OSC port
		const char* osc_port = get_current_osc_port();
		mvprintw(0, COLS - 30, "[CPU] %.1f%%  [OSC:%s]", cpu, osc_port);

		int rows, cols;
		getmaxyx(stdscr, rows, cols);

        int base_module_height = 3;
		int module_spacing = 1; // padding b/w modules
        int module_count = get_module_count();
		int modules_per_col = (rows - 4) / (base_module_height + module_spacing);
		if (modules_per_col < 1) modules_per_col = 1;
		
		int col_heights[64] = {0}; // vertical offset per col

        for (int i = 0; i < module_count; i++) {
            Module* m = get_module(i);
            if (m && m->draw_ui) {
				int col = i / modules_per_col;
				int module_height = base_module_height;
				if (strcmp(m->name, "looper") == 0) module_height = 6; // add exceptions here
				int y = 2 + col_heights[col]; 
				int x = 2 + col * COLUMN_WIDTH;

                if (i == focused_module_index)
                    attron(A_REVERSE);
				move(y,x);
                m->draw_ui(m,y,x);
                if (i == focused_module_index)
                    attroff(A_REVERSE);
				col_heights[col] += module_height + module_spacing;
            }
        }

        if (in_command_mode) {
            mvprintw(LINES - 2, 2, ": %s", command);
        } else {
            mvprintw(LINES - 2, 2, "[TAB] switch module | [:q] quit | [:] command mode");
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
            } else if (ch == 27) { // ESC
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
            } else if (ch == ':') {
                in_command_mode = 1;
                cmd_index = 0;
                command[0] = '\0';
                if (focused && focused->handle_input)
                    focused->handle_input(focused, ch);
            } else {
                if (focused && focused->handle_input)
                    focused->handle_input(focused, ch);
            }
        }
    }

    endwin();
}

