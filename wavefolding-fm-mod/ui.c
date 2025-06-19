#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"

void *ui_thread(void *arg) {
    FMState *state = (FMState*)arg;

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
            if (ch == KEY_LEFT) state->modulator_freq -= 0.5f;
            else if (ch == KEY_RIGHT) state->modulator_freq += 0.5f;
            else if (ch == '+') state->fold_threshold_mod += 0.01f;
            else if (ch == '_') state->fold_threshold_mod -= 0.01f;
            else if (ch == '=') state->fold_threshold_car += 0.01f;
            else if (ch == '-') state->fold_threshold_car -= 0.01f;
            else if (ch == ']') state->blend += 0.01f;
            else if (ch == '[') state->blend -= 0.01f;
            else if (ch == KEY_UP) state->index += 0.01f;
            else if (ch == KEY_DOWN) state->index -= 0.01f;

            if (state->fold_threshold_mod < 0.01f) state->fold_threshold_mod = 0.01f;
            if (state->fold_threshold_mod > 1.0f) state->fold_threshold_mod = 1.0f;
            if (state->fold_threshold_car < 0.01f) state->fold_threshold_car = 0.01f;
            if (state->fold_threshold_car > 1.0f) state->fold_threshold_car = 1.0f;
            if (state->blend < 0.01f) state->blend = 0.01f;
            if (state->blend > 1.0f) state->blend = 1.0f;
            if (state->index < 0.01f) state->index = 0.01f;
            pthread_mutex_unlock(&state->lock);

            if (ch == ':') {
                entering_command = true;
                memset(command, 0, sizeof(command));
                cmd_index = 0;
            }

            clear();
            mvprintw(1,2,"Terminal FM Wavefolding Modulator");
            mvprintw(3,2,"[<]/[>] or key f: Mod Freq        (%.2f Hz)", state->modulator_freq);
            mvprintw(4,2,"[^]/[v] or key i: Mod Index       (%.2f)", state->index);
            mvprintw(5,2,"[+]/[_] or key m: Mod Fold Amt    (%.2f)", state->fold_threshold_mod);
            mvprintw(6,2,"[=]/[-] or key c: Car (In) Fold Amt (%.2f)", state->fold_threshold_car);
            mvprintw(7,2,"[[]/[]] or key b: Blend Amt       (%.2f)", state->blend);
            mvprintw(8,2,"Text commands also active. Press ':' to enter text mode.");
        } else {
            if (ch == '\n') {
                entering_command = false;
                char type; float val;
                if (sscanf(command, "%c %f", &type, &val) == 2) {
                    pthread_mutex_lock(&state->lock);
                    if (type == 'f') state->modulator_freq = val;
                    else if (type == 'i') state->index = val;
                    else if (type == 'm') state->fold_threshold_mod = val;
                    else if (type == 'c') state->fold_threshold_car = val;
                    else if (type == 'b') state->blend = val;
                    pthread_mutex_unlock(&state->lock);
                } else if (strcmp(command, "q") == 0) {
                    endwin(); exit(0);
                }
            } else if (ch == 27) {
                entering_command = false;
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                if (cmd_index > 0) command[--cmd_index] = '\0';
            } else if (ch != ERR && cmd_index < sizeof(command) - 1) {
                command[cmd_index++] = ch;
                command[cmd_index] = '\0';
            }
            mvprintw(12,2,": %s", command);
        }

        refresh();
        struct timespec ts = {0, 50000000};
        nanosleep(&ts, NULL);
    }
    endwin();
    return NULL;
}

