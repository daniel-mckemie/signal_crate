#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <math.h>

#include "module.h"
#include "util.h"
#include "vca.h"

static void vca_process(Module* m, float* in, unsigned long frames) {
    VCAState* state = (VCAState*)m->state;
    float* inL  = m->inputs[0];
    float* inR  = m->inputs[1];
    float* outL = m->output_bufferL;
    float* outR = m->output_bufferR;

    // --- 1) Read params once ---
    pthread_mutex_lock(&state->lock);
    float base_gain = state->gain;
    float base_pan  = state->pan;
    pthread_mutex_unlock(&state->lock);

    float gain = base_gain;
    float pan  = base_pan;

    // --- 2) Apply CV modulation (unsmoothed) ---
    float mod_depth = 1.0f;
    for (int i = 0; i < m->num_control_inputs; i++) {
        if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

        const char* param = m->control_input_params[i];
        float control = *(m->control_inputs[i]);

        if (strcmp(param, "gain") == 0) {
            float norm = fminf(fmaxf(control, 0.0f), 1.0f);
            float mod_range = (1.0f - base_gain) * mod_depth;
            gain = base_gain + norm * mod_range;

        } else if (strcmp(param, "pan") == 0) {
            float norm_pan = fminf(fmaxf(control, 0.0f), 1.0f);
            norm_pan = norm_pan * 2.0f - 1.0f;   // 0–1 → -1 to +1
            pan = base_pan + norm_pan * (1.0f - fabsf(base_pan));
        }
    }

    // --- 3) Clamp modulated values ---
    gain = fminf(fmaxf(gain, 0.0f), 1.0f);
    pan  = fminf(fmaxf(pan, -1.0f), 1.0f);

    // --- 4) Smooth ONCE per audio block (correct behavior) ---
    float gain_s = process_smoother(&state->smooth_gain, gain);
    float pan_s  = process_smoother(&state->smooth_pan,  pan);

    // Update UI displays
    state->display_gain = gain_s;
    state->display_pan  = pan_s;

    // --- 5) If no audio, output silence ---
    if (!inL && !inR) {
        memset(outL, 0, frames * sizeof(float));
        memset(outR, 0, frames * sizeof(float));
        return;
    }

    // --- 6) Per-sample audio processing (NO smoothing here) ---
    for (unsigned long i = 0; i < frames; i++) {
        float left  = inL ? inL[i] : 0.0f;
        float right = inR ? inR[i] : left;  // duplicate if mono

        // Pan law: equal-power panning
        float angle = (pan_s + 1.0f) * M_PI_4;
        float l_gain = cosf(angle);
        float r_gain = sinf(angle);

        outL[i] = gain_s * l_gain * left;
        outR[i] = gain_s * r_gain * right;

        // Safety clamp
        outL[i] = fmaxf(fminf(outL[i], 1.0f), -1.0f);
        outR[i] = fmaxf(fminf(outR[i], 1.0f), -1.0f);
    }
}

static void clamp_params(VCAState* state) {
    clampf(&state->gain, 0.0f, 1.0f);
    clampf(&state->pan, -1.0f, 1.0f);
}

static void vca_draw_ui(Module* m, int y, int x) {
    VCAState* state = (VCAState*)m->state;

    pthread_mutex_lock(&state->lock);
    float gain = state->display_gain;
    float pan = state->display_pan;
    pthread_mutex_unlock(&state->lock);

	BLUE();
    mvprintw(y, x, "[VCA:%s] ", m->name); 
	CLR();

	LABEL(2	, "pan:");
	ORANGE(); printw(" %.2f | ", pan); CLR();
	
	LABEL(2	, "gain:");
	ORANGE(); printw(" %.2f | ", gain); CLR();

	YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= pan, [/] gain");
    mvprintw(y+2, x, "Command mode: :1 [pan], :2 [gain]");
	BLACK();
}

static void vca_handle_input(Module* m, int key) {
    VCAState* state = (VCAState*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '-': state->pan -= 0.01f; handled = 1; break;
            case '=': state->pan += 0.01f; handled = 1; break;
            case '[': state->gain -= 0.01f; handled = 1; break;
            case ']': state->gain += 0.01f; handled = 1; break;
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
                if (type == '1') state->pan= val;
				else if (type == '2') state->gain = val;
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

    // Clamp
    if (handled) clamp_params(state);
	state->display_gain = state->gain;
	state->display_pan = state->pan;

    pthread_mutex_unlock(&state->lock);
}

static void vca_set_osc_param(Module* m, const char* param, float value) {
    VCAState* state = (VCAState*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "gain") == 0) {
        state->gain = fmaxf(value, 0.0f);
	} else if (strcmp(param, "pan") == 0) {
		float p = fminf(fmaxf(value, 0.0f), 1.0f);
		state->pan = p * 2.0f - 1.0f; 
    } else {
        fprintf(stderr, "[vca] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&state->lock);
}

static void vca_destroy(Module* m) {
    VCAState* state = (VCAState*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float gain = 1.0f;
	float pan = 0.0f;
	int ch = -1; // default stereo

	if (args && strstr(args, "gain=")) {
        sscanf(strstr(args, "gain="), "gain=%f", &gain);
    }
	if (args && strstr(args, "pan=")) {
        sscanf(strstr(args, "pan="), "pan=%f", &pan);
    }
	if (args && strstr(args, "ch=")) {
        sscanf(strstr(args, "ch="), "ch=%d", &ch);
    }

    VCAState* state = calloc(1, sizeof(VCAState));
	state->gain = gain;
    state->pan = pan;
	state->target_channel = ch; // default = stereo
    pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_gain, 0.75);
	init_smoother(&state->smooth_pan, 0.75);

    Module* m = calloc(1, sizeof(Module));
    m->name = "vca";  // IMPORTANT: engine uses "out" for final audio
    m->state = state;
	m->output_bufferL = calloc(MAX_BLOCK_SIZE, sizeof(float));  // mono
	m->output_bufferR = calloc(MAX_BLOCK_SIZE, sizeof(float));  // mono
    m->process = vca_process;
    m->draw_ui = vca_draw_ui;
    m->handle_input = vca_handle_input;
    m->set_param = vca_set_osc_param;
    m->destroy = vca_destroy;
    return m;
}

