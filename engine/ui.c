#include <ncurses.h>
#include "engine.h"

void ui_loop() {
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);

    while (1) {
        clear();
        printw("--- Modular Synth Engine ---\n");
        for (int i = 0; i < module_count; i++) {
            mvprintw(i + 2, 2, "Module %d: %s", i, chain[i]->name);
            if (chain[i]->draw_ui) chain[i]->draw_ui(chain[i], i + 4);
        }
        refresh();
        napms(100);
    }

    endwin();
}

