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
	TemplateState* state = (TemplateState)m->state;
	float param1, param2;
	CustomType cust_type;
	
	// Set variables to states for easier reading
	pthread_mutex_lock(&state->lock); // Lock thread
	p1 = process_smoother(&state->smooth_p1, state->param1);
	p2 = process_smoother(&state->smooth_p2, state->param2);
	cust_type = state->cust_type;
	pthread_mutex_unlock(&state->lock); // Unlock thread
	
	// Control input thread
	// This method is designed to be very general, with params with
	// control input defined in the second if/else if statement below...
	float param1 = 1.0f; // Depth output for control modules
	for (int i=0; i<m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inpyts[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

		if (strcmp(param, "param1") == 0) {
			// Keeps the range clamped by default of 0-1.0f
			float mod_range = state->param1; 
			p1 = state->param1 + norm * mod_range;
		} else if (strcmp(param, "param2") == 0) {
			float mod_range = (3.0 - state->param2); // An example if range is 0.0-3.0f
			p2 = state->param2 + norm * mod_range;
		}
	}

	// If control module, you need to send out the param
	// via the CONTROL thread, not the AUDIO thread!
	m->control_output_depth = fminf(fmaxf(depth, 0.0f), 1.0f);

	if (!m->control_output) return;

	// Have UI always reflect the proper value, regardless of control input
	state->display_param1 = p1;
	state->display_param2 = p2;
	
	// Audio processing starts here
	// Set "global" utility variables if needed...
	float global1 = 1.0f;
	float global2 = 2.0f;
	for (unsigned long i=0; i<frames; i++) {
		float input_sample = in[i]; // Declare input

		if (!isfinite(input_sample)) input_sample = 0.0f;
		float x = tanhf(input_sample); // Input limiter
		// Have some other audio processes here if you need, if you do
		// then use another tanhf below for soft saturation, otherwise
		// it is redundant...
		x -= k * audio_process_xyz;
		x = tanhf(x); // Soft saturation

		float y; // assignable variable for writing output
		switch (CustType) {
			case TYPE1:
				y = some_output_item; break;
			case TYPE2:
				y = another_output_item; break;
			case TYPE3:
				y = yet_another_output_item; break;
		}

		m->output_buffer[i] = fminf(fmaxf(y, -1.0f), 1.0f); // output, normalized to -1.0 - 1.0f
	}
}

// Boundaries for params
static void clamp_params(TemplateState *state) {
	clampf(&state->param1, 0.0f, 1.0f);  // Clamp between 0-1
	clampf(&state->param2, -1.0f, 1.0f); // Clamp between -1-1
	// If a param involves frequency boundaries, clamping against
	// the sample rate dynamically -- just under the Nyquist freq
	clampf(&state>frequency_param, 0.1f, state->sample_rate * 0.45f);
}

// ncurses and UI definitions
static void template_draw_ui(Module* m, int y, int x) {
	TemplateState* state = (TemplateState*)m->state;

	float param1, param2;
	char cmd[64] = ""; // Command string

	// Thread for command input in UI
	pthread_mutex_lock(&state->lock);
	param1 = state->param1;
	param2 = state->param2;

	if (state->entering_command)
		snprintf(cmd, sizeof(cmd), "%s", state->command_buffer);
	pthread_mutex_unlock(&state->lock);

	// ModuleName:Alias, parameters, then passing in vals for name/alias and params
	mvprintw(y,   x, "[Template:%s] param1: %.2f | param2: %s", m->name, param1, param2);
	mvprintw(y+1, x, "Real-time keys: -/= (param1), _/+ (param2), d/D (depth)"); // Sometimes depth is assumed, in ctrl modules especially
	mvprintw(y+2, x, "Command mode: :1 [param1], :2 [param2], :d[depth]"); // Sometimes depth is assumed, in ctrl modules especially
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
            case 'd': state->depth += 0.01f; handled = 1; break;
			case 'D': state->depth -= 0.01f; handled = 1; break;
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
	
	// Takes in args in script to set params through .txt file or declaratively
	if (args && strstr(args, "param1=")) {
		sscanf(strstr(args, "param1="), "param1=%f", &param1);
	}

	if (args && strstr(args, "param2=")) {
		sscanf(strstr(args, "param2="), "param2=%f", &param2);
	}

	TemplateState s = calloc(1, sizeof(TemplateState));
	s->param1 = param1;
	s->param2 = param2;
	s->depth = depth; // if applicable (control modules)
	s->sample_rate = sample_rate;

	// init mutex and smoother on params
	pthread_mutex_init(&s->lock, NULL);
	init_smoother(&s->smooth_param1, 0.75f);
	init_smoother(&s->smooth_param2, 0.75f);
	clamp_params(s);

	Module* m = calloc(1, sizeof(Module));
	m->name = "template";
	m->state = s;
	m->process_control = template_process_control;
	m->draw_ui = template_draw_ui;
	m->handle_input = template_handle_input;
	m->set_param = template_set_osc_param;
	m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->destroy = template_destroy;
	return m;
}
