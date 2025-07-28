#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "delay.h"
#include "module.h"
#include "util.h"

#define MAX_DELAY_MS 2000

static void delay_process(Module* m, float* in, unsigned long frames) {
    Delay* state = (Delay*)m->state;

    float mix, fb, delay_ms;
    pthread_mutex_lock(&state->lock);
    mix = process_smoother(&state->smooth_mix, state->mix);
    fb = process_smoother(&state->smooth_feedback, state->feedback);
    delay_ms = process_smoother(&state->smooth_delay, state->delay_ms);
    pthread_mutex_unlock(&state->lock);

    // Control-rate modulation - non-destructive
	float mod_depth = 1.0f;
    for (int i = 0; i < m->num_control_inputs; i++) {
        if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

        const char* param = m->control_input_params[i];
        float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);	

        if (strcmp(param, "time") == 0) {
			float mod_range = state->delay_ms * mod_depth;
            // delay_ms = min_ms * powf(max_ms / min_ms, control); // exponential map
            delay_ms = state->delay_ms + norm * mod_range;
        } else if (strcmp(param, "mix") == 0) {
			float mod_range = (1.0f - state->mix) * mod_depth;
            mix = state->mix + norm * mod_range;
        } else if (strcmp(param, "fb") == 0) {
			float mod_range = (1.0f - state->feedback) * mod_depth;
            fb = state->feedback + norm * mod_range;
        }
    }

    // For display
    state->display_mix = mix;
    state->display_feedback = fb;
    state->display_delay = delay_ms;

    unsigned int buffer_size = state->buffer_size;
    unsigned int write_index = state->write_index;

    // Smooth delay time per sample
    float target_delay_samples = (delay_ms / 1000.0f) * state->sample_rate;
    if (target_delay_samples > (float)(buffer_size - 1)) target_delay_samples = (float)(buffer_size - 1);
    if (target_delay_samples < 1.0f) target_delay_samples = 1.0f;

    float delay_samples = state->last_delay_samples;
    float smoothing = 0.001f;  // smaller is smoother; increase if too sluggish

    for (unsigned long i = 0; i < frames; i++) {
        // Smooth toward target
        delay_samples += smoothing * (target_delay_samples - delay_samples);

        float read_pos = (float)write_index - delay_samples;
        if (read_pos < 0.0f) read_pos += buffer_size;

        unsigned int base = (unsigned int)read_pos;
        float frac = read_pos - base;
        float s1 = state->buffer[base % buffer_size];
        float s2 = state->buffer[(base + 1) % buffer_size];
        float delayed = (1.0f - frac) * s1 + frac * s2;

        float dry = in[i];
        float out = dry * (1.0f - mix) + delayed * mix;
        m->output_buffer[i] = out;

        state->buffer[write_index] = dry + delayed * fb;

        write_index = (write_index + 1) % buffer_size;
    }

    state->write_index = write_index;
    state->last_delay_samples = delay_samples;
}

static void clamp_params(Delay* state) {
    clampf(&state->mix, 0.0f, 1.0f);
    clampf(&state->feedback, 0.0f, 0.99f);
    clampf(&state->delay_ms, 1.0f, MAX_DELAY_MS);
}

static void delay_draw_ui(Module* m, int y, int x) {
    Delay* state = (Delay*)m->state;
    char cmd[64] = "";

    pthread_mutex_lock(&state->lock);
    float mix = state->display_mix;
    float fb = state->display_feedback;
    float ms = state->display_delay;
    if (state->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", state->command_buffer);
    pthread_mutex_unlock(&state->lock);

    mvprintw(y,   x, "[Delay:%s] Time: %.1f ms | Mix: %.2f | Feedback: %.2f", m->name, ms, mix, fb);
    mvprintw(y+1, x, "Real-time keys: -/= (time), _/+ (mix), [/] (fb)");
    mvprintw(y+2, x, "Command mode: :1 [time], :2 [mix], :3 [fb]");
    if (cmd[0])
        mvprintw(y+3, x, "%s", cmd);
}

static void delay_handle_input(Module* m, int key) {
    Delay* state = (Delay*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);
    if (!state->entering_command) {
        switch (key) {
            case '=': state->delay_ms += 10.0f; handled = 1; break;
            case '-': state->delay_ms -= 10.0f; handled = 1; break;
            case '+': state->mix += 0.05f; handled = 1; break;
            case '_': state->mix -= 0.05f; handled = 1; break;
            case ']': state->feedback += 0.05f; handled = 1; break;
            case '[': state->feedback -= 0.05f; handled = 1; break;
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
                if (type == '1') state->delay_ms = val;
                else if (type == '2') state->mix = val;
                else if (type == '3') state->feedback = val;
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

    if (handled) clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void delay_set_osc_param(Module* m, const char* param, float value) {
    Delay* state = (Delay*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "time") == 0) {
        state->delay_ms = fmaxf(1.0f, fminf(value, 2000.0f));
    } else if (strcmp(param, "mix") == 0) {
        state->mix = fmaxf(0.0f, fminf(value, 1.0f));
    } else if (strcmp(param, "fb") == 0) {
        state->feedback = fmaxf(0.0f, fminf(value, 0.99f));
    } else {
        fprintf(stderr, "[delay] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&state->lock);
}

static void delay_destroy(Module* m) {
    Delay* state = (Delay*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float delay_ms = 500.0f;
	float mix = 0.5f;
	float feedback = 0.3f;
    if (args && strstr(args, "time=")) {
        sscanf(strstr(args, "time="), "time=%f", &delay_ms);
	}
	if (args && strstr(args, "mix=")) {
        sscanf(strstr(args, "mix="), "mix=%f", &mix);
    }
	if (args && strstr(args, "fb=")) {
        sscanf(strstr(args, "fb="), "fb=%f", &feedback);
    }

    Delay* state = calloc(1, sizeof(Delay));
    state->sample_rate = sample_rate;
    state->delay_ms = delay_ms;
    state->mix = mix;
    state->feedback = feedback;
    state->buffer_size = (unsigned int)((MAX_DELAY_MS / 1000.0f) * sample_rate);
    state->buffer = calloc(state->buffer_size, sizeof(float));
	state->last_delay_samples = (state->delay_ms / 1000.0f) * sample_rate;
    pthread_mutex_init(&state->lock, NULL);
    init_smoother(&state->smooth_delay, 0.75f);
    init_smoother(&state->smooth_mix, 0.75f);
    init_smoother(&state->smooth_feedback, 0.75f);
    clamp_params(state);

    Module* m = calloc(1, sizeof(Module));
    m->name = "delay";
    m->state = state;
    m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = delay_process;
    m->draw_ui = delay_draw_ui;
    m->handle_input = delay_handle_input;
	m->set_param = delay_set_osc_param;
    m->destroy = delay_destroy;
    return m;
}

