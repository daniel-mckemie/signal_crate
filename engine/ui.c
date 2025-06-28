#include <ncurses.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "ui.h"
#include "module.h"

extern Module* chain[MAX_MODULES];
extern int module_count;

void ui_loop() {
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    char command[128] = "";
    int cmd_index = 0;
    int in_command_mode = 0;
    int running = 1;

    while (running) {
        clear();
        mvprintw(0, 2, "--- Modular Synth Engine ---");
        for (int i = 0; i < module_count; i++) {
            if (chain[i]->draw_ui)
                chain[i]->draw_ui(chain[i], 2 + i * 4);
        }

        if (in_command_mode) {
            mvprintw(LINES - 2, 2, ": %s", command);
        } else {
            mvprintw(LINES - 2, 2, "[:q] quit, [:] command mode");
        }

        refresh();
        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);

        int ch = getch();
        if (ch == ERR) continue;

        if (in_command_mode) {
            if (ch == '\n') {
                command[cmd_index] = '\0';

                if (strcmp(command, "q") == 0) {
                    running = 0;
                    break;
                }

                for (int i = 0; i < module_count; i++) {
                    if (chain[i]->handle_input) {
                        for (int j = 0; j < (int)strlen(command); j++) {
                            chain[i]->handle_input(chain[i], command[j]);
                        }
                        chain[i]->handle_input(chain[i], '\n');
                    }
                }

                cmd_index = 0;
                command[0] = '\0';
                in_command_mode = 0;

            } else if (ch == 27) {  // ESC
                cmd_index = 0;
                command[0] = '\0';
                in_command_mode = 0;
            } else if ((ch == KEY_BACKSPACE || ch == 127) && cmd_index > 0) {
                command[--cmd_index] = '\0';
            } else if (cmd_index < sizeof(command) - 1 && ch >= 32 && ch < 127) {
                command[cmd_index++] = ch;
                command[cmd_index] = '\0';
            }

        } else {
            if (ch == ':') {
                in_command_mode = 1;
                cmd_index = 0;
                command[0] = '\0';

                // Notify all modules to enter command mode (optional)
                for (int i = 0; i < module_count; i++) {
                    if (chain[i]->handle_input)
                        chain[i]->handle_input(chain[i], ':');
                }
            } else {
				// Forward normal key presses to all modules
				for (int i = 0; i < module_count; i++) {
					if (chain[i]->handle_input)
						chain[i]->handle_input(chain[i], ch);
				}
			}
        }
    }

    endwin();
}

