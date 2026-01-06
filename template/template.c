#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h> // Multithreading library
#include <ncurses.h> // UI library

// Operational headers and custom libraries
#include "template.h"
#include "module.h"
#include "util.h"


// Assuming module name = C file name = label throughout for methods
static void template_process(Module *m, float* in, unsigned long frames) {
	TemplateState* state = (TemplateState*)m->state;
	CustomType cust_type;
	float* input = (m->num_inputs > 0) ? m->inputs[0] : in; // One input, for > 1 in see: amp_mod
	float* out   = m->output_buffer; // One output
	
	// Set base params in thread lock
	pthread_mutex_lock(&state->lock); // Lock thread
	float base_param1 = state->param1;
	float base_param2 = state->param2;
	float sample_rate = state->sample_rate; // Declare if you want to alias state params
	cust_type = state->cust_type;
	pthread_mutex_unlock(&state->lock); // Unlock thread
	
	// Smooth params to base, outside of thread
	float param1_s = process_smoother(&state->smooth_p1, base_param1);
	float param2_s = process_smoother(&state->smooth_p2, base_param2);

	// Set display/last param for reading/callback in UI
	float disp_param1 = param1_s;
	float disp_param2 = param2_s;

	// Control input thread
	// Outer loop sets params and does DSP
	// Inner loop reads CV ins and bubbles them up
	for (int i=0; i<frames; i++) {
		// Declare DSP params, as displays/last read of block
		float param1 = param1_s;
		float param2 = param2_s;

		// CV loop...CV read at sample rate, not block rate
		for (int j=0; j<m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			// CV read in three ways...
			if (strcmp(param, "param1") == 0) {
				param1 += control; // Reads CV as is, [0-1]
			} else if (strcmp(param, "param2") == 0) {
				param2 += control * 4.0f; // Scales to full range of param
				// OR
				param2 += control * base_param2;. // Relative to knob position, good for freq/cutoff/etc
			}
		}

		// Clamp
		clampf(&param1,  0.0f, 1.0f);  // Clamp between 0-1
		clampf(&param2, -1.0f, 1.0f); // Clamp between -1-1
		clampf(&param3,  0.0f, sample_rate * 0.45f); // Clamp at Nyquist
		clampf(&param4,  0.0f, 4.0f); // Clamp between 0-4, etc...

		// Set display to last block value
		disp_param1 = param1;
		disp_param2 = param2;

		// ------****** DSP FUNCTIONS HERE ******------- //
		// ------****** DSP FUNCTIONS HERE ******------- //
		// ------****** DSP FUNCTIONS HERE ******------- //
		// ------****** DSP FUNCTIONS HERE ******------- //
		// ------****** DSP FUNCTIONS HERE ******------- //

		float in_s = input ? input[i] : 0.0f; // Input for use: for > 1 in, see: amp_mod
		float val = calculated_final_output; // Final DSP output calculated set here, for clarity
	    out[i] = val; // Final output
	}
	// Set display/last values back to UI
	pthread_mutex_lock(&state->lock);
	state->display_param1 = disp_param1;
	state->display_param2 = disp_param2;
	pthread_mutex_unlock(&state->lock);
}

// Boundaries for params
static void clamp_params(TemplateState *state) {
	clampf(&state->param1, 0.0f, 1.0f);  // Clamp between 0-1
	clampf(&state->param2, -1.0f, 1.0f); // Clamp between -1-1
	// If a param involves frequency boundaries, clamping against
	// the sample rate dynamically -- just under the Nyquist freq
	clampf(&state->frequency_param, 0.1f, state->sample_rate * 0.45f);
}

// ncurses and UI definitions
static void template_draw_ui(Module* m, int y, int x) {
	TemplateState* state = (TemplateState*)m->state;
	const char* cust_names[] = {"Name1", "Name2", "Name3"}; // If ENUM type

	float param1, param2;
	CustomType cust_type;

	// Thread for command input in UI
	pthread_mutex_lock(&state->lock);
	param1 = state->display_param1;
	param2 = state->display_param2;
	cust_type = state->cust_type;
	pthread_mutex_unlock(&state->lock);

	BLUE();
    mvprintw(y, x, "[Module Name:%s] ", m->name);
	CLR();

	LABEL(2, "param1:");
	ORANGE(); printw(" %.2f Hz | ", param1); CLR();

	LABEL(2, "param2:");
	ORANGE(); printw(" %.2f | ", param2); CLR();

	LABEL(2, "cust_type:");
	ORANGE(); printw(" %s", cust_names[cust_type]); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= (cutoff), _/+ (res)");
    mvprintw(y+2, x, "Command mode: :1 [cutoff], :2 [res] f: [type]");
	BLACK();
}

// Keyboard and command control
static void template_handle_input(Module* m, int key) {
	TemplateState* state = (TemplateState*)m->state;
	int handled = 0; // Allows for clean breaks in case

	pthread_mutex_lock(&state->lock);

	if (!state->entering_command) {
		switch (key) {
			// Each param matched with keystrokes and amount param is changed
			case '=': state->param1 += 0.05f; handled = 1; break;
            case '-': state->param1 -= 0.05f; handled = 1; break;
            case '+': state->param2 += 0.01f; handled = 1; break;
            case '_': state->param2 -= 0.01f; handled = 1; break;
            case 'D': state->depth += 0.01f; handled = 1; break; // D for + depth in ctrl
			case 'd': state->depth -= 0.01f; handled = 1; break; // d for - depth in ctrl
			// Enter command mode, to type cmd # and value :20.9, puts param2 = 0.9
			case ':':
					  state->entering_command = true;
					  memset(state->command_buffer, 0, sizeof(state->command_buffer));
					  state->command_index = 0;
					  handled = 1;
					  break;
		}
	} else { // This block handles the command mode functionality
        if (key == '\n') {
            state->entering_command = false;
            char type;
            float val;
            if (sscanf(state->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') state->param1 = val;
                else if (type == '2') state->param2 = val;
                else if (type == 'd') state->depth = val;
            }
			handled = 1;
        } else if (key == 27) {
            state->entering_command = false;
			handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && state->command_index > 0) {
            state->command_index--;
            state->command_buffer[state->command_index] = '\0';
			handled = 1;
        } else if (key >= 32 && key < 127 && state->command_index < sizeof(state->command_buffer) - 1) {
            state->command_buffer[state->command_index++] = (char)key;
            state->command_buffer[state->command_index] = '\0';
			handled = 1;
        }
    }

	if (handled)
		clamp_params(state); // ensure params don't exceed boundaries
	pthread_mutex_unlock(&state->lock); // unlock UI thread
}
							  
// OSC parameter assignments/definitions
static void template_set_osc_param(Module* m, const char* param, float value) {
	TemplateState* state = (TemplateState*)m->state;
	pthread_mutex_lock(&state->lock); // Lock thread for OSC param

	// OSC provides 0.0-1.0f, should be scaled per param
	if (strcmp(param, "param1") == 0) {
		float min_hz = 1.0f; // If your param involves frequency...
		float max_hz = 20000.0f; // Provides extra boundaries to prevent crashing
		float norm = fminf(fmaxf(value, 0.0f), 1.0f); // clamp
		state->param1 = min_hz * powf(max_hz / min_hz, norm);
	} else if (strcmp(param, "param2") == 0) {
		state->param2 = fminf(fmaxf(value, 0.0f), 1.0f);
	} else {
		fprintf(stderr, "[Template] Unown OSC param: %s\n", param);
	}
	clamp_params(state);
	pthread_mutex_unlock(&state->lock);
}


// Call to module.h for proper cleanup upon close
static void template_destroy(Module* m) {
	TemplateState* state = (TemplateState*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
	destroy_base_module(m);
}

// create module with params method
Module* create_module(const char* args, float sample_rate) {
	float param1 = 440.0f;
	float param2 = 1.0f;
	CustomType cust_type = OPTION1;
	
	// Takes in args in script to set params through .txt file or declaratively
	if (args && strstr(args, "param1=")) {
		sscanf(strstr(args, "param1="), "param1=%f", &param1);
	}

	if (args && strstr(args, "param2=")) {
		sscanf(strstr(args, "param2="), "param2=%f", &param2);
	}

	TemplateState* s = calloc(1, sizeof(TemplateState));
	s->param1 = param1;
	s->param2 = param2;
	s->cust_type = cust_type;
	s->depth = depth; // if applicable (control modules use depth on output)
	s->sample_rate = sample_rate;

	// init mutex and smoother on params
	pthread_mutex_init(&s->lock, NULL);
	init_smoother(&s->smooth_param1, 0.75f);
	init_smoother(&s->smooth_param2, 0.75f);
	clamp_params(s);

	Module* m = calloc(1, sizeof(Module));
	m->name = "template";
	m->state = s;
	m->process = template_process;
	m->draw_ui = template_draw_ui;
	m->handle_input = template_handle_input;
	m->set_param = template_set_osc_param;
	m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float)); // For Control c_ modules only
	m->destroy = template_destroy;
	return m;
}
