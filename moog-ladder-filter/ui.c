#include <ncurses.h> // UI in terminal
#include <pthread.h> // Threading for UI with audio
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"

void *ui_thread(void *arg) {
	MoogFilter *state = (MoogFilter*)arg;

	initscr(); cbreak(); noecho();
	keypad(stdscr, TRUE);
	nodelay(stdscr, TRUE);

	char command[100] = "";
	bool entering_command = false;
	int cmd_index = 0;

	while(state->running) {
		int ch = getch();
		if (!entering_command) {
			pthread_mutex_lock(&state->lock); // Lock state for input
			if (ch == '0') state->cutoff += 0.5f; 
			else if (ch == '9') state->cutoff -= 0.5f; 
			else if (ch == ')') state->resonance += 0.01f; 
			else if (ch == '(') state->resonance -= 0.01f; 

			// Set boundaries for params
			if (state->cutoff < 10.0f) state->cutoff = 10.0f;
			if (state->cutoff > state->sample_rate * 0.45f) state->cutoff = state->sample_rate * 0.45f;
			if (state->resonance < 0.0f) state->resonance = 0.0f;
			if (state->resonance > 4.2f) state->resonance = 4.2f;      

			pthread_mutex_unlock(&state->lock); // Unlock once complete

			// Hit ':' key to change to text input mode
			if (ch == ':') {
				entering_command = true;
				memset(command, 0, sizeof(command));
				cmd_index = 0;
			}

			// Prints out info in terminal, (xpos,ypos,"Info" --> state)
			clear();
			mvprintw(1,2,"Moog Ladder Filter");
			mvprintw(3,2,"[9]/[0] or key c: Cutoff (%.2f Hz)", state->cutoff);
			mvprintw(4,2,"[(]/[)] or key r: Resonance (%.2f)", state->resonance);
		} else {
			if (ch == '\n') { // Carriage return sends command, omit it
				entering_command = false;
				char type; float val;
				if (sscanf(command, "%c %f", &type, &val) == 2) {
					pthread_mutex_lock(&state->lock); 
					if (type == 'c') state->cutoff = val; 
					else if (type == 'r') state->resonance = val; 
					pthread_mutex_unlock(&state->lock); 
				} else if (strcmp(command, "q") == 0) {
					pthread_mutex_lock(&state->lock);
					state->running = 0; 
					pthread_mutex_unlock(&state->lock);
					endwin();
					return NULL;  // Exit app
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
		struct timespec ts = {0, 50000000};	// Sleep for 50ms for commands to set
		nanosleep(&ts, NULL);
	}
	endwin();
	return NULL;
}


