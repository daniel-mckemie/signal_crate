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
	AudioModuleName *state = (AudioModuleName*)arg;


		if (!entering_command) {
			pthread_mutex_lock(&state->lock); // Lock state for input
			if (ch == KEY_LEFT) state->param1 += 0.5f; // key, param, amt change
			else if (ch == 'k') state->param2 -= 0.5f;  // can also define specifc key

			// Set boundaries for params
			if (state->param1 < 0.01f) state->param1 = 0.01f;
			if (state->param2 > 1.0f) state->param2 = 1.0f;      

			pthread_mutex_unlock(&state->lock); // Unlock once complete

			// Hit ':' key to change to text input mode
			if (ch == ':') {
				entering_command = true;
				memset(command, 0, sizeof(command));
				cmd_index = 0;
			}

			// Prints out info in terminal, (xpos,ypos,"Info" --> state)
			clear();
			mvprintw(1,2,"Title of the instrument");
			mvprintw(3,2,"[<]/[>] or key f: Param 1        (%.2f Hz)", state->param1);
			mvprintw(4,2,"[^]/[v] or key i: Param 2       (%.2f)", state->param2);
		} else {
			if (ch == '\n') { // Carriage return sends command, omit it
				entering_command = false;
				char type; float val;
				if (sscanf(command, "%c %f", &type, &val) == 2) {
					pthread_mutex_lock(&state->lock); // Lock state for input
					if (type == 'f') state->param1 = val; // Press f followed by val <f 102>
					else if (type 'i') state->param2 = val; // Press i for param2 <i 10>
					pthread_mutex_unlock(&state->lock); // Unlock state
				} else if (strcmp(command, "q") == 0) {
					pthread_mutex_lock(&state->lock);
					state->running = 0; // Updates to NOT running for a clean quit
					pthread_mutex_unlock(&state->lock);
					endwin();
					return NULL  // Exit app
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
		struct timespec ts = {0, 50000000} // Sleep for 50ms for commands to set
		nanosleep(&ts, NULL);
	}
	endwin();
	return NULL;
}
