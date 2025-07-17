#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "wavefolder.h"
#include "module.h"
#include "util.h"

float fold(float x, float amt) {
    // Optional: add rectification or nonlinear behavior
    x = fabsf(x);  // full-wave rectify
    x = tanhf(x * amt); // nonlinear shaping

    while (fabsf(x) > amt) {
        if (x > amt) {
            x = 2 * amt - x;
        } else {
            x = -2 * amt - x;
        }
    }
    return x;
}

static void wavefolder_process(Module *m, float* in, unsigned long frames) {
    Wavefolder *state = (Wavefolder*)m->state;

    float fa, blend, drive;
    pthread_mutex_lock(&state->lock);
    fa = process_smoother(&state->smooth_amt, state->fold_amt);
    blend = process_smoother(&state->smooth_blend, state->blend);
    drive = process_smoother(&state->smooth_drive, state->drive);
    pthread_mutex_unlock(&state->lock);

	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);

		if (strcmp(param, "fold_amt") == 0) {
			fa = control * 5.0f;  // range [0, 5]
		} else if (strcmp(param, "blend") == 0) {
			blend = control;  // range [0, 1]
		} else if (strcmp(param, "drive") == 0) {
			drive = control * 10.0f;  // range [0, 10]
		}
	}

	state->display_fold_amt = fa;
	state->display_blend = blend;
	state->display_drive= drive;
	

    for (unsigned long i=0; i<frames; i++) {
		float input = in[i];
        float warped = input * drive;
        float f = fold(warped, fa);
        m->output_buffer[i] = (1.0f - blend) * input + blend * f;
    }
}

static void wavefolder_draw_ui(Module *m, int y, int x) {
    Wavefolder *state = (Wavefolder*)m->state;

    float fold_amt, blend, drive;

    pthread_mutex_lock(&state->lock);
    fold_amt = state->display_fold_amt;
    blend = state->display_blend;
    drive = state->display_drive;
    pthread_mutex_unlock(&state->lock);

    mvprintw(y, x, "[Wavefolder] Fold_Amt: %.2f, Blend: %.2f, Drive: %.2f", fold_amt, blend, drive);
    mvprintw(y+1, x, "Real-time keys: -/= (fold_amt), _/+ (blend), [/] (drive)");
    mvprintw(y+2, x, "Command mode: :1 [fold amt], :2 [blend], :3 [drive]");
}

static void clamp_params(Wavefolder *state) {
    if (state->fold_amt < 0.01f) state->fold_amt = 0.01f;
    if (state->fold_amt > 5.0f)  state->fold_amt = 5.0f;

    if (state->blend < 0.01f) state->blend = 0.01f;
    if (state->blend > 1.0f)  state->blend = 1.0f;

    if (state->drive < 0.01f) state->drive = 0.01f;
    if (state->drive > 10.0f) state->drive = 10.0f;
}

static void wavefolder_handle_input(Module *m, int key) {
    Wavefolder *state = (Wavefolder*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->fold_amt += 0.01f; handled = 1; break;
            case '-': state->fold_amt -= 0.01f; handled = 1; break;
            case '+': state->blend += 0.01f; handled = 1; break;
            case '_': state->blend -= 0.01f; handled = 1; break;
            case ']': state->drive += 0.01f; handled = 1; break;
            case '[': state->drive -= 0.01f; handled = 1; break;
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
                if (type == '1') state->fold_amt = val;
                else if (type == '2') state->blend = val;
                else if (type == '3') state->drive = val;
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

static void wavefolder_set_osc_param(Module* m, const char* param, float value) {
    Wavefolder* state = (Wavefolder*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "fold_amt") == 0) {
        state->fold_amt = fminf(fmaxf(value * 5.0f, 0.01f), 5.0f);  // map [0–1] → [0.01–5]
    } else if (strcmp(param, "blend") == 0) {
        state->blend = fminf(fmaxf(value, 0.01f), 1.0f);
    } else if (strcmp(param, "drive") == 0) {
        state->drive = fminf(fmaxf(value * 10.0f, 0.01f), 10.0f);  // map [0–1] → [0.01–10]
    } else {
        fprintf(stderr, "[wavefolder] Unknown OSC param: %s\n", param);
    }

    clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void wavefolder_destroy(Module* m) {
    if (!m) return;
    Wavefolder* state = (Wavefolder*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
    Wavefolder *state = calloc(1, sizeof(Wavefolder));
    state->fold_amt = 0.5f;
    state->blend = 0.01f;
    state->drive = 1.0f;
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
    init_smoother(&state->smooth_amt, 0.75f);
    init_smoother(&state->smooth_blend, 0.75f);
    init_smoother(&state->smooth_drive, 0.75f);
    clamp_params(state);

    Module *m = calloc(1, sizeof(Module));
    m->name = "wavefolder";
    m->state = state;
    m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = wavefolder_process;
    m->draw_ui = wavefolder_draw_ui;
    m->handle_input = wavefolder_handle_input;
	m->set_param = wavefolder_set_osc_param;
    m->destroy = wavefolder_destroy;
    return m;
}

