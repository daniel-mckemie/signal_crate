#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vco_ui.h"

void *vco_ui_thread(void *arg) {
    VCO *state = (VCO*)arg;

    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    char command[100] = "";
    bool entering_command = false;
    int cmd_index = 0;

    while (1) {
        int ch = getch();
        if (!entering_command) {
            pthread_mutex_lock(&state->lock);
            if (ch == '0') state->frequency += 0.5f;
            else if (ch == '9') state->frequency -= 0.5f;
            else if (ch == ')') state->amplitude += 0.01f;
            else if (ch == '(') state->amplitude -= 0.01f;
            else if (ch == 'w') state->waveform = (state->waveform + 1) % 4;
            pthread_mutex_unlock(&state->lock);

            clear();
            mvprintw(1, 2, "[VCO] Press ':' for command, 'q' to quit.");
            mvprintw(3, 2, "Freq: %.2f", state->frequency);
            mvprintw(4, 2, "Amp : %.2f", state->amplitude);
            mvprintw(5, 2, "Wave: %d", state->waveform);

            if (ch == ':') {
                entering_command = true;
                memset(command, 0, sizeof(command));
                cmd_index = 0;
            }
        } else {
            if (ch == '\n') {
                entering_command = false;
                char type; float val;
                if (sscanf(command, "%c %f", &type, &val) == 2) {
                    pthread_mutex_lock(&state->lock);
                    if (type == 'f') state->frequency = val;
                    else if (type == 'a') state->amplitude = val;
                    else if (type == 'w') state->waveform = ((int)val) % 4;
                    pthread_mutex_unlock(&state->lock);
                }
            } else if (ch == 27) {
                entering_command = false;
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmd_index > 0) command[--cmd_index] = '\0';
            } else if (ch != ERR && cmd_index < sizeof(command) - 1) {
                command[cmd_index++] = ch;
                command[cmd_index] = '\0';
            }
            mvprintw(12, 2, ": %s", command);
        }

        refresh();
        struct timespec ts = {0, 50000000};
        nanosleep(&ts, NULL);
    }

    endwin();
    return NULL;
}
