#include <ncurses.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include "module.h"
#include "engine.h"

void ui_loop() {
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    int focused_module_index = 0;
    int in_command_mode = 0;
    char command[128] = "";
    int cmd_index = 0;
    int running = 1;

    while (running) {
        clear();
        mvprintw(0, 2, "--- Flow Control ---");

        int module_padding = 9;
        int module_count = get_module_count();  // NEW
        for (int i = 0; i < module_count; i++) {
            Module* m = get_module(i);  // NEW
            if (m && m->draw_ui) {
                if (i == focused_module_index)
                    attron(A_REVERSE);
                m->draw_ui(m, 2 + i * module_padding);
                if (i == focused_module_index)
                    attroff(A_REVERSE);
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

