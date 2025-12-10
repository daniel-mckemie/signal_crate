#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>
#include <string.h>

#include "amp_mod.h"
#include "module.h"
#include "util.h"

static void ampmod_process(Module* m, float* in, unsigned long frames) {
	AmpMod* state = (AmpMod*)m->state;

	float* in_car = m->inputs[0];
	float* in_mod = m->inputs[1];
	float* out = m->output_buffer;

	if (!in_car || !in_mod) {
		memset(out, 0, frames * sizeof(float));
		return;
	}

	pthread_mutex_lock(&state->lock);
	float target_car = state->car_amp;
	float target_mod = state->mod_amp;
	float target_depth   = state->depth;
	pthread_mutex_unlock(&state->lock);


	float car_amp = process_smoother(&state->smooth_car_amp, target_car);
	float mod_amp = process_smoother(&state->smooth_mod_amp, target_mod);
	float depth   = process_smoother(&state->smooth_depth,   target_depth);

	// Non-destructive control input
	float mod_depth = 1.0f;
	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

		if (strcmp(param, "mod_amp") == 0) {
			float mod_range = (1.0f - state->mod_amp) * mod_depth;
			mod_amp = state->mod_amp + norm * mod_range;
		} else if (strcmp(param, "car_amp") == 0) {
			float mod_range = (1.0f - state->car_amp) * mod_depth;
			car_amp = state->car_amp + norm * mod_range;
		} else if (strcmp(param, "depth") == 0) {
			float mod_range = (1.0f - state->depth) * mod_depth;
			depth = state->depth + norm * mod_range;

		}
	}
	mod_amp = fminf(fmaxf(mod_amp, 0.0f), 1.0f);
	car_amp = fminf(fmaxf(car_amp, 0.0f), 1.0f);

	for (unsigned long i = 0; i < frames; i++) {
		float c = in_car[i] * car_amp;
		float mval = in_mod[i] * mod_amp;
		// float mod_factor = (1.0f - depth) + (depth * mval);
		float mod_factor = (1.0f - depth) + depth * (0.5f * (mval + 1.0f));
		out[i] = c * mod_factor;
	}

	state->display_car_amp = car_amp;
	state->display_mod_amp = mod_amp;
	state->display_depth   = depth;
}

static void clamp_params(AmpMod *state) {
	clampf(&state->car_amp, 0.0f, 1.0f);
	clampf(&state->mod_amp, 0.0f, 1.0f);
	clampf(&state->depth,   0.0f, 1.0f);
}

static void ampmod_draw_ui(Module* m, int y, int x) {
	AmpMod* state = (AmpMod*)m->state;

	float car_amp, mod_amp, depth;
	char cmd[64] = "";

	pthread_mutex_lock(&state->lock);
	car_amp = state->display_car_amp;
	mod_amp = state->display_mod_amp;
	depth   = state->display_depth;
	if (state->entering_command)
		snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
	pthread_mutex_unlock(&state->lock);

	BLUE();
	mvprintw(y,x,"[AmpMod:%s] ", m->name);
	CLR();

	LABEL(2, "Car_Amp:");
	ORANGE(); printw(" %.2f | ", car_amp); CLR();

	LABEL(2, "Mod_Amp:");
	ORANGE(); printw(" %.2f | ", mod_amp); CLR();

	LABEL(2, "Depth:");
	ORANGE(); printw(" %.2f", depth); CLR();

	YELLOW();
	mvprintw(y+1, x, "Real-time keys: -/= (Car_Amp), _/+ (Mod_Amp), [/] (Depth)");
	mvprintw(y+2, x, "Command mode: :1 [car_amp], :2 [mod_amp], :d [depth]");
	BLACK();
}

static void ampmod_handle_input(Module* m, int key) {
	AmpMod* state = (AmpMod*)m->state;
	int handled = 0;

	pthread_mutex_lock(&state->lock);

	if (!state->entering_command) {
		switch (key) {
			case '+': state->mod_amp += 0.01f; handled = 1; break;
			case '_': state->mod_amp -= 0.01f; handled = 1; break;
			case '=': state->car_amp += 0.01f; handled = 1; break;
			case '-': state->car_amp -= 0.01f; handled = 1; break;
			case ']': state->depth   += 0.01f; handled = 1; break;
			case '[': state->depth   -= 0.01f; handled = 1; break;
			case ':':
				state->entering_command = true;
				memset(state->command_buffer, 0, sizeof(state->command_buffer));
				state->command_index = 0;
				handled = 1;
				break;
		}
	} else {
		if (key == '\n') {
			state->entering_command = false;
			char type;
			float val;
			if (sscanf(state->command_buffer, "%c %f", &type, &val) == 2) {
				if (type == '1') state->car_amp = val;
				else if (type == '2') state->mod_amp = val;
				else if (type == 'd') state->depth   = val;
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
		clamp_params(state);
	pthread_mutex_unlock(&state->lock);
}

static void amp_mod_set_osc_param(Module* m, const char* param, float value) {
	AmpMod* state = (AmpMod*)m->state;
	pthread_mutex_lock(&state->lock);

	if (strcmp(param, "car_amp") == 0)
		state->car_amp = fminf(fmaxf(value, 0.0f), 1.0f);
	else if (strcmp(param, "mod_amp") == 0)
		state->mod_amp = fminf(fmaxf(value, 0.0f), 1.0f);
	else if (strcmp(param, "depth") == 0)
		state->depth = fminf(fmaxf(value, 0.0f), 1.0f);
	else
		fprintf(stderr, "[amp_mod] Unknown OSC param: %s\n", param);

	pthread_mutex_unlock(&state->lock);
}

static void ampmod_destroy(Module* m) {
	AmpMod* state = (AmpMod*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
	destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float car_amp = 1.0f;
	float mod_amp = 1.0f;
	float depth = 1.0f;

	if (args && strstr(args, "car_amp="))
		sscanf(strstr(args, "car_amp="), "car_amp=%f", &car_amp);
	if (args && strstr(args, "mod_amp="))
		sscanf(strstr(args, "mod_amp="), "mod_amp=%f", &mod_amp);
	if (args && strstr(args, "depth="))
		sscanf(strstr(args, "depth="), "depth=%f", &depth);

	AmpMod* state = calloc(1, sizeof(AmpMod));
	state->car_amp = car_amp;
	state->mod_amp = mod_amp;
	state->depth = depth;
	state->sample_rate = sample_rate;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_car_amp, 0.75f);
	init_smoother(&state->smooth_mod_amp, 0.75f);
	init_smoother(&state->smooth_depth, 0.75f);
	clamp_params(state);

	Module* m = calloc(1, sizeof(Module));
	m->name = "amp_mod";
	m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
	m->process = ampmod_process;
	m->draw_ui = ampmod_draw_ui;
	m->handle_input = ampmod_handle_input;
	m->set_param = amp_mod_set_osc_param;
	m->destroy = ampmod_destroy;
	return m;
}

