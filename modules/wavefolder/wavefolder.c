#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "wavefolder.h"
#include "module.h"
#include "util.h"

static inline float folder(float x, float amt, float *lp_z, float sample_rate) {
	// 2x oversampling - analog style
    if (amt <= 0.0f) return 0.0f;
    x = tanhf(x * amt);
    float x0 = x;
    float xm = 0.5f * x0;

    float y0 = tanhf(x0);
    float y1 = tanhf(xm);

    // simple one-pole lowpass before decimation
    float cutoff = 0.45f * sample_rate;
    float a = cutoff / (cutoff + sample_rate);

    *lp_z += a * (y0 - *lp_z);
    *lp_z += a * (y1 - *lp_z);

    return *lp_z;
}

static void wavefolder_process(Module *m, float* in, unsigned long frames) {
    Wavefolder *state = (Wavefolder*)m->state;
	float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
	float* out   = m->output_buffer;

    pthread_mutex_lock(&state->lock);
    float base_fold  = state->fold;
    float base_blend = state->blend;
    float base_drive = state->drive;
    pthread_mutex_unlock(&state->lock);

    float fold_s  = process_smoother(&state->smooth_fold,  base_fold);
    float blend_s = process_smoother(&state->smooth_blend, base_blend);
    float drive_s = process_smoother(&state->smooth_drive, base_drive);

	float disp_fold  = fold_s;
	float disp_blend = blend_s;
	float disp_drive = drive_s;

    for (unsigned long i=0; i<frames; i++) {
		float fold  = fold_s;
		float blend = blend_s;
		float drive = drive_s;

		for (int j=0; j<m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "fold") == 0) {
				fold += control * 5.0;
			} else if (strcmp(param, "blend") == 0) {
				blend += control; 
			} else if (strcmp(param, "drive") == 0) {
				drive += control* 10.0f;
			}
		}

		clampf(&fold,  0.01f, 5.0f);
		clampf(&blend, 0.0f,  1.0f);
		clampf(&drive, 0.01f, 10.0f);

		disp_fold  = fold;
		disp_blend = blend;
		disp_drive = drive;

		float in_s = input ? input[i] : 0.0f;
		float f = folder(in_s * drive, fold, &state->lp_z, state->sample_rate);
		float val = (1.0f - blend) * in_s + blend * f;
		out[i] = val;
	}
	pthread_mutex_lock(&state->lock);
	state->display_fold   = disp_fold;
	state->display_blend  = disp_blend;
	state->display_drive  = disp_drive;
	pthread_mutex_unlock(&state->lock);
}

static void clamp_params(Wavefolder *state) {
    clampf(&state->fold, 0.01f, 5.0f);
    clampf(&state->blend,    0.0f,  1.0f);
    clampf(&state->drive,    0.01f, 10.0f);
}

static void wavefolder_draw_ui(Module *m, int y, int x) {
    Wavefolder *state = (Wavefolder*)m->state;

    float fold, blend, drive;

    pthread_mutex_lock(&state->lock);
    fold = state->display_fold;
    blend = state->display_blend;
    drive = state->display_drive;
    pthread_mutex_unlock(&state->lock);

	BLUE();
    mvprintw(y, x, "[Wavefolder:%s] ", m->name);
	CLR();
	
	LABEL(2, "fold:");
	ORANGE(); printw(" %.2f | ", fold); CLR();
	
	LABEL(2, "blend:");
	ORANGE(); printw(" %.2f | ", blend); CLR();

	LABEL(2, "drive:");
	ORANGE(); printw(" %.2f", drive); CLR();
	
	YELLOW();
    mvprintw(y+1, x, "Real-time keys: -/= (fold), _/+ (blend), [/] (drive)");
    mvprintw(y+2, x, "Command mode: :1 [fold], :2 [blend], :3 [drive]");
	BLACK();
}

static void wavefolder_handle_input(Module *m, int key) {
    Wavefolder *state = (Wavefolder*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
            case '=': state->fold += 0.01f; handled = 1; break;
            case '-': state->fold -= 0.01f; handled = 1; break;
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
                if (type == '1') state->fold = val;
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

    float norm = fminf(fmaxf(value, 0.0f), 1.0f);
    if (strcmp(param, "fold") == 0) {
        state->fold = 0.01f + norm * (5.0f - 0.01f);
    } else if (strcmp(param, "blend") == 0) {
        state->blend = norm;
    } else if (strcmp(param, "drive") == 0) {
        state->drive = 0.01f + norm * (10.0f - 0.01f);
    }
    clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void wavefolder_destroy(Module* m) {
    Wavefolder* state = (Wavefolder*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float fold = 0.5f;
	float blend = 0.0f;
	float drive = 1.0f;

	if (args && strstr(args, "fold=")) {
        sscanf(strstr(args, "fold="), "fold=%f", &fold);
    }
    if (args && strstr(args, "blend=")) {
        sscanf(strstr(args, "blend="), "blend=%f", &blend);
	}
	if (args && strstr(args, "drive=")) {
        sscanf(strstr(args, "drive="), "drive=%f", &drive);
    }

    Wavefolder *state = calloc(1, sizeof(Wavefolder));
    state->fold = fold;
    state->blend = blend;
    state->drive = drive;
    state->sample_rate = sample_rate;
    pthread_mutex_init(&state->lock, NULL);
    init_smoother(&state->smooth_fold, 0.75f);
    init_smoother(&state->smooth_blend, 0.75f);
    init_smoother(&state->smooth_drive, 0.75f);
    clamp_params(state);

    Module *m = calloc(1, sizeof(Module));
    m->name = "wavefolder";
    m->state = state;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = wavefolder_process;
    m->draw_ui = wavefolder_draw_ui;
    m->handle_input = wavefolder_handle_input;
	m->set_param = wavefolder_set_osc_param;
    m->destroy = wavefolder_destroy;
    return m;
}

