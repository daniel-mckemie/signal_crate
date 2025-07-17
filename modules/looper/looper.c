#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>
#include <string.h>

#include "looper.h"
#include "module.h"
#include "util.h"

static void looper_process(Module* m, float* in, unsigned long frames) {
    Looper* state = (Looper*)m->state;

    pthread_mutex_lock(&state->lock);
    float* buffer = state->buffer;
    float read_pos = state->read_pos;
    unsigned long loop_start = state->loop_start;
    unsigned long loop_end = state->loop_end;
    float playback_speed = process_smoother(&state->smooth_speed, state->playback_speed);
	state->current_speed_display = playback_speed;
    LooperState current_state = state->looper_state;
    pthread_mutex_unlock(&state->lock);

    for (int i = 0; i < m->num_control_inputs; i++) {
        if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

        const char* param = m->control_input_params[i];
        float control = *(m->control_inputs[i]);

        if (strcmp(param, "speed") == 0) {
            float min = 0.1f;
            float max = 4.0f;
            playback_speed = min * powf(max / min, control);
        }
    }

    for (unsigned long i = 0; i < frames; i++) {
		float input = in[i];
        float output = 0.0f;

        switch (current_state) {
            case RECORDING:
                buffer[state->write_pos % state->buffer_len] = input;
				output = input; // monitor input
                state->write_pos++;
                if (state->write_pos >= loop_end) state->write_pos = loop_start;
                break;

            case PLAYING: {
                int i1 = (int)read_pos % state->buffer_len;
                int i2 = (i1 + 1) % state->buffer_len;
                float frac = read_pos - (float)i1;
                output = (1.0f - frac) * buffer[i1] + frac * buffer[i2];
				// Fade in at loop start to avoid click
				if (read_pos < state->loop_start + 32) {
					float fade = (read_pos - state->loop_start) / 32.0f;
					if (fade < 0.0f) fade = 0.0f;
					if (fade > 1.0f) fade = 1.0f;
					output *= fade;
				}
                read_pos += playback_speed;
                if (read_pos >= loop_end) read_pos = loop_start;
                break;
            }

            case OVERDUBBING: {
                int widx = state->write_pos % state->buffer_len;
                buffer[widx] += input; // overdub
                state->write_pos++;
                int i1 = (int)read_pos % state->buffer_len;
                int i2 = (i1 + 1) % state->buffer_len;
                float frac = read_pos - (float)i1;
                output = (1.0f - frac) * buffer[i1] + frac * buffer[i2];
                read_pos += playback_speed;
                if (read_pos >= loop_end) read_pos = loop_start;
                break;
            }

            case STOPPED:
            case IDLE:
            default:
                output = 0.0f;
                break;
        }

        m->output_buffer[i] = output;
    }

    pthread_mutex_lock(&state->lock);
    state->read_pos = read_pos;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(Looper *state) {
	unsigned long max_samples = state->buffer_len;
    if (state->loop_start > max_samples - 1) state->loop_start = max_samples - 1;
    if (state->loop_end > max_samples) state->loop_end = max_samples;
    if (state->loop_end < state->loop_start) state->loop_end = state->loop_start + 1; // force valid loop
    if (state->playback_speed < 0.1f) state->playback_speed = 0.1f;
    if (state->playback_speed > 4.0f) state->playback_speed = 4.0f;
}

static void looper_draw_ui(Module* m, int y, int x) {
    Looper* state = (Looper*)m->state;

    static const char* state_names[] = {
        "IDLE", "RECORDING", "PLAYING", "OVERDUBBING", "STOPPED"
    };

    pthread_mutex_lock(&state->lock);

    LooperState lstate = state->looper_state;
    float speed = state->current_speed_display;
    float sr = state->sample_rate;

    float start_sec = state->loop_start / sr;
    float end_sec   = state->loop_end   / sr;
	float pos_sec;

	if (lstate == RECORDING) {
		float write_sec = state->write_pos / sr;
		if (write_sec < start_sec) write_sec = start_sec;
		if (write_sec > end_sec)   write_sec = end_sec;
		pos_sec = write_sec;
	} else {
		float read_sec = state->read_pos / sr;
		if (read_sec < start_sec) read_sec = start_sec;
		if (read_sec > end_sec)   read_sec = end_sec;
		pos_sec = read_sec;
	}

    char cmd[64];
    strncpy(cmd, state->command_buffer, sizeof(cmd));
    cmd[sizeof(cmd) - 1] = '\0'; // Null-terminate

    pthread_mutex_unlock(&state->lock);

    mvprintw(y,     x, "[Looper] State: %s | Speed: %.2fx", state_names[lstate], speed);
    mvprintw(y + 1, x, "         Loop Range: [%.2f -> %.2f] sec", start_sec, end_sec);
    mvprintw(y + 2, x, "         Current Position: %.2f sec", pos_sec);
    mvprintw(y + 3, x, "Function keys: -/= (start pt), _/+ (end pt), [/] (speed)");
    mvprintw(y + 4, x, "State keys: r(record), p(play), o(overdub), s(stop)");
    mvprintw(y + 5, x, "Cmd: :1=start :2=end :3=speed");
}

static void looper_handle_input(Module* m, int ch) {
    Looper* state = (Looper*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (state->entering_command) {
        if (ch == 10 || ch == '\r') {  // ENTER
            char type = state->command_buffer[0];
            float val = atof(&state->command_buffer[1]);

            if (type == '1') state->loop_start = (unsigned long)(val * state->sample_rate);
            else if (type == '2') state->loop_end = (unsigned long)(val * state->sample_rate);
            else if (type == '3') state->playback_speed = val;

            state->entering_command = false;
            state->command_index = 0;
            state->command_buffer[0] = '\0';

            clamp_params(state);
            handled = 1;
        } else if (ch == 27) {  // ESC
            state->entering_command = false;
            state->command_index = 0;
            state->command_buffer[0] = '\0';
            handled = 1;
        } else if (state->command_index < (int)(sizeof(state->command_buffer) - 1)) {
            state->command_buffer[state->command_index++] = (char)ch;
            state->command_buffer[state->command_index] = '\0';
            handled = 1;
        }
    } else {
        switch (ch) {
			case '=': state->loop_start += (unsigned long)(0.1 * state->sample_rate); handled=1; break;
			case '-': state->loop_start -= (unsigned long)(0.1 * state->sample_rate); handled=1; break;
			case '+': state->loop_end += (unsigned long)(0.1 * state->sample_rate); handled=1; break;
			case '_': state->loop_end -= (unsigned long)(0.1 * state->sample_rate); handled=1; break;
			case ']': state->playback_speed += 0.05; handled=1; break;
			case '[': state->playback_speed -= 0.05; handled=1; break;
            case ':':
                state->entering_command = true;
                state->command_index = 0;
                state->command_buffer[0] = '\0';
                handled = 1;
                break;
            case 'r': state->looper_state = RECORDING; handled=1; break;
            case 'p': state->looper_state = PLAYING; handled=1; break;
            case 'o': state->looper_state = OVERDUBBING; handled=1; break;
            case 's': state->looper_state = STOPPED; handled=1; break;
        }
    }

	if (handled)
		clamp_params(state);

    pthread_mutex_unlock(&state->lock);
}

static void looper_destroy(Module* m) {
    if (!m) return;
    Looper* state = (Looper*)m->state;
    if (state) {
        pthread_mutex_destroy(&state->lock);
        free(state);
    }
}

Module* create_module(float sample_rate) {
    Looper* state = calloc(1, sizeof(Looper));
    state->sample_rate = sample_rate;
	state->buffer_len = (unsigned long)(sample_rate * 10.0f); // 10 second buffer
	state->buffer = calloc(state->buffer_len, sizeof(float));														  
    state->loop_start = 0;
    state->loop_end = (unsigned long)(sample_rate * 10.0f); 
    state->playback_speed = 1.0f;
	state->write_pos = 0;
    pthread_mutex_init(&state->lock, NULL);
	init_sine_table();
    init_smoother(&state->smooth_start, 0.75f);
    init_smoother(&state->smooth_end, 0.75f);
    init_smoother(&state->smooth_speed, 0.75f);
    clamp_params(state);

    Module* m = calloc(1, sizeof(Module));
    m->name = "looper";
    m->state = state;
	m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = looper_process;
    m->draw_ui = looper_draw_ui;
    m->handle_input = looper_handle_input;
    m->destroy = looper_destroy;
    return m;
}
