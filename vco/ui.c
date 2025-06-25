#include <ncurses.h> // UI in terminal
#include <pthread.h> // Threading for UI with audio
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ui.h"

// This lets you use keys and input text commands
// for changing params. It also provides visual
// feedback. All in terminal.
void *ui_thread(void *arg) {
	VCO *state = (VCO*)arg;

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
			if (ch == '0') state->frequency += 0.5f; 
			else if (ch == '9') state->frequency -= 0.5f;  
			else if (ch == ')') state->amplitude += 0.01f;  
			else if (ch == '(') state->amplitude -= 0.01f;

			const char *wave_names[] = {"Sine", "Saw", "Square", "Triangle"};
			
			// Boundaries function called here, defined in audio.c
			clamp_params(state);
			pthread_mutex_unlock(&state->lock); // Unlock once complete

			// Hit ':' key to change to text input mode
			if (ch == ':') {
				entering_command = true;
				memset(command, 0, sizeof(command));
				cmd_index = 0;
			}

			// Prints out info in terminal, (xpos,ypos,"Info" --> state)
			clear();
			mvprintw(1,2,"Voltage Controlled Oscillator");
			mvprintw(3,2,"[9]/[0] or key f: freq        (%.2f Hz)", state->frequency);
			mvprintw(4,2,"[(]/[)] or key a: amp         (%.2f)", state->amplitude);
			mvprintw(5,2,"key w: waveform (0=sin,1=saw,2=sq,3=tri (%s)", wave_names[state->waveform]);
		} else {
			if (ch == '\n') { // Carriage return sends command, omit it
				entering_command = false;
				char type; float val;
				if (sscanf(command, "%c %f", &type, &val) == 2) {
					pthread_mutex_lock(&state->lock); // Lock state for input
					if (type == 'f') state->frequency = val;
					else if (type == 'a') state->amplitude = val;
					else if (type == 'w') state->waveform = ((int)val) % 4; 
					clamp_params(state); // boundaries call also here, defined in vco.c
					pthread_mutex_unlock(&state->lock); // Unlock state
				} else if (strcmp(command, "q") == 0) {
					pthread_mutex_lock(&state->lock);
					state->running = 0; // Updates to NOT running for a clean quit
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
