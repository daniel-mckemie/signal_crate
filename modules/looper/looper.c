#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>
#include <string.h>

#include "looper.h"
#include "module.h"
#include "util.h"

static void looper_process(Module* m, float* in, unsigned long frames) {
    Looper* s = (Looper*)m->state;
	float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
	float* out = m->output_buffer;

	pthread_mutex_lock(&s->lock);
	float* buffer      = s->buffer;
	unsigned long buffer_len = s->buffer_len;
	float read_pos     = s->read_pos;
	unsigned long write_pos  = s->write_pos;
	unsigned long loop_start = s->loop_start;
	unsigned long loop_end   = s->loop_end;
	float base_speed = s->playback_speed;
	float base_amp   = s->amp;
	LooperState current_state = s->looper_state;
	pthread_mutex_unlock(&s->lock);

	float playback_speed_s = process_smoother(&s->smooth_speed, base_speed);
	float amp_s            = process_smoother(&s->smooth_amp,   base_amp);

	float disp_playback_speed = playback_speed_s;
	float disp_amp			  = amp_s;
	float disp_read_pos		  = read_pos;

    for (unsigned long i=0; i<frames; i++) {
		float playback_speed = playback_speed_s;
		float amp            = amp_s;
		
		for (int j=0; j<m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "speed") == 0) {
				playback_speed += control * 4.0f;
			}
			else if (strcmp(param, "amp") == 0) {
				amp += control;
			}
		}

		clampf(&playback_speed, 0.1f, 4.0f);
		clampf(&amp, 0.0, 1.0f);

		disp_playback_speed = playback_speed;
		disp_amp			= amp;

		float in_s = input ? input[i] : 0.0f;
		float output = 0.0f;

		switch (current_state) {
			case RECORDING:
				buffer[write_pos % buffer_len] = in_s;
				output = in_s; // monitor input
				write_pos++;
				if (write_pos >= loop_end) write_pos = loop_start;
				break;

			case PLAYING: {
				int i1 = (int)read_pos % buffer_len;
				int i2 = (i1 + 1) % buffer_len;
				float frac = read_pos - (float)i1;
				output = (1.0f - frac) * buffer[i1] + frac * buffer[i2];
				// Fade in at loop start to avoid click
				if (read_pos < loop_start + 32) {
					float fade = (read_pos - loop_start) / 32.0f;
					if (fade < 0.0f) fade = 0.0f;
					if (fade > 1.0f) fade = 1.0f;
					output *= fade;
				}
				read_pos += playback_speed;
				if (read_pos >= loop_end) read_pos = loop_start;
				break;
			}

			case OVERDUBBING: {
				int widx = write_pos % buffer_len;
				buffer[widx] += in_s; // overdub
				write_pos++;
				int i1 = (int)read_pos % buffer_len;
				int i2 = (i1 + 1) % buffer_len;
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

		disp_read_pos = read_pos;
		float val = output * amp;
		out[i] = val;
	}

    pthread_mutex_lock(&s->lock);
    s->read_pos = disp_read_pos;
    s->write_pos = write_pos;
	s->display_playback_speed = disp_playback_speed;
	s->display_amp = disp_amp;
    pthread_mutex_unlock(&s->lock);
}

static void clamp_params(Looper *state) {
    unsigned long max_samples = state->buffer_len;
    if (state->loop_start > max_samples - 1)
        state->loop_start = max_samples - 1;
    if (state->loop_end > max_samples)
        state->loop_end = max_samples;
    if (state->loop_end < state->loop_start)
        state->loop_end = state->loop_start + 1;
    clampf(&state->playback_speed, 0.1f, 4.0f);
    clampf(&state->amp, 0.0, 1.0f);
}


static void looper_draw_ui(Module* m, int y, int x) {
    Looper* state = (Looper*)m->state;

    static const char* state_names[] = {
        "IDLE", "RECORDING", "PLAYING", "OVERDUBBING", "STOPPED"
    };

    pthread_mutex_lock(&state->lock);

    LooperState lstate = state->looper_state;
    float speed = state->display_playback_speed;
    float amp = state->display_amp;
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

	BLUE();
	mvprintw(y,x, "[Looper:%s] ", m->name);
	CLR();

	LABEL(2,"");
	ORANGE(); printw(" %s|", state_names[lstate]); CLR();
	LABEL(2, "range:");
	ORANGE(); printw(" %.2f->%.2f|", start_sec, end_sec); CLR();
	LABEL(2, "sp:");
	ORANGE(); printw(" %.2fx|", speed); CLR();
	LABEL(2, "pos:");
	ORANGE(); printw(" %.2f|", pos_sec); CLR();
	LABEL(2, "amp:");
	ORANGE(); printw(" %.2f", amp); CLR();

	YELLOW();
    mvprintw(y + 1, x, "-/= (st), _/+ (end), [/] (sp), r/p/o/s (rec/play/odub/stop)");
    mvprintw(y + 2, x, "Cmd Mode: :1=start, :2=end, :3=speed, :4=amp");
	BLACK();
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
            else if (type == '4') state->amp = val;

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
			case '}': state->amp += 0.01; handled=1; break;
			case '{': state->amp -= 0.01; handled=1; break;
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

static void looper_set_osc_param(Module* m, const char* param, float value) {
    Looper* state = (Looper*)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "speed") == 0) {
        float min = 0.1f, max = 4.0f;
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        state->playback_speed = min * powf(max / min, norm);
    } else if (strcmp(param, "amp") == 0) {
		state->amp = value;
    } else if (strcmp(param, "start") == 0) {
        state->loop_start = (unsigned long)(value * state->sample_rate);
    } else if (strcmp(param, "end") == 0) {
        state->loop_end = (unsigned long)(value * state->sample_rate);
    } else if (strcmp(param, "record") == 0 && value > 0.5f) {
        state->looper_state = RECORDING;
    } else if (strcmp(param, "play") == 0 && value > 0.5f) {
        state->looper_state = PLAYING;
    } else if (strcmp(param, "overdub") == 0 && value > 0.5f) {
        state->looper_state = OVERDUBBING;
    } else if (strcmp(param, "stop") == 0 && value > 0.5f) {
        state->looper_state = STOPPED;
    } else {
        fprintf(stderr, "[looper] Unknown OSC param: %s\n", param);
    }

    clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void looper_destroy(Module* m) {
    Looper* state = (Looper*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float loop_length = 10.0f;
	float playback_speed = 1.0f;
	float amp = 1.0f;

    if (args && strstr(args, "length=")) {
        sscanf(strstr(args, "length="), "length=%f", &loop_length);
	}
	if (args && strstr(args, "speed=")) {
        sscanf(strstr(args, "speed="), "speed=%f", &playback_speed);
    }
	if (args && strstr(args, "amp=")) {
        sscanf(strstr(args, "amp="), "amp=%f", &amp);
    }

    Looper* state = calloc(1, sizeof(Looper));
    state->sample_rate = sample_rate;
	state->buffer_len = (unsigned long)(sample_rate * loop_length); // 10 second buffer
	state->buffer = calloc(state->buffer_len, sizeof(float));														  
    state->loop_start = 0;
    state->loop_end = (unsigned long)(sample_rate * loop_length); 
    state->playback_speed = playback_speed;
	state->amp = amp;
	state->write_pos = 0;
    pthread_mutex_init(&state->lock, NULL);
	init_sine_table();
    init_smoother(&state->smooth_speed, 0.75f);
    init_smoother(&state->smooth_amp, 0.75f);
    clamp_params(state);

    Module* m = calloc(1, sizeof(Module));
    m->name = "looper";
    m->state = state;
	m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = looper_process;
    m->draw_ui = looper_draw_ui;
    m->handle_input = looper_handle_input;
	m->set_param = looper_set_osc_param;
    m->destroy = looper_destroy;
    return m;
}
