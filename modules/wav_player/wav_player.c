#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <ncurses.h>
#include <sndfile.h>
#include <libgen.h>

#include "wav_player.h"
#include "module.h"
#include "util.h"

static void player_process(Module* m, float* in, unsigned long frames) {
    Player* s = (Player*)m->state;
    float* out = m->output_buffer;

    pthread_mutex_lock(&s->lock);
    unsigned long max_frames = s->num_frames;
    float pos         = s->playing ? s->play_pos : s->external_play_pos;
    float scrub_target= s->scrub_target;
    float base_speed  = s->playback_speed;
    float base_amp    = s->amp;
    bool  playing     = s->playing;
    float file_rate   = s->file_rate;
    float sr          = s->sample_rate;
    pthread_mutex_unlock(&s->lock);

    float speed_s = process_smoother(&s->smooth_speed, base_speed);
    float amp_s   = process_smoother(&s->smooth_amp,   base_amp);

    float disp_speed = speed_s;
    float disp_amp   = amp_s;
    float disp_pos   = pos;

    if (max_frames < 2) {
        for (unsigned long i = 0; i < frames; i++) out[i] = 0.0f;
        return;
    }

    clampf(&scrub_target, 0.0f, (float)(max_frames - 1));
    clampf(&pos,          0.0f, (float)(max_frames - 1));

    for (unsigned long i = 0; i < frames; i++) {
        float speed = speed_s;
        float amp   = amp_s;

        for (int j=0; j<m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "speed") == 0) {
                speed += control * 4.0f;
            } else if (strcmp(param, "amp") == 0) {
                amp += control;
            } else if (strcmp(param, "scrub") == 0) {
                scrub_target += control * (0.1f * (float)max_frames);
            }
        }

        clampf(&speed, 0.1f, 4.0f);
        clampf(&amp,   0.0f, 1.0f);
        clampf(&scrub_target, 0.0f, (float)(max_frames - 1));

        disp_speed = speed;
        disp_amp   = amp;

        if (!playing) {
            pos = scrub_target;
        }

        if (pos < 0.0f) pos = 0.0f;
        if (pos > (float)(max_frames - 2)) pos = (float)(max_frames - 2);

        int i1 = (int)pos;
        int i2 = i1 + 1;
        float frac = pos - (float)i1;

        float s1 = s->data[i1];
        float s2 = s->data[i2];

        float val = (1.0f - frac) * s1 + frac * s2;
        out[i] = val * amp;

        if (playing) {
            pos += speed * (file_rate / sr);
            if (pos >= (float)(max_frames - 1)) pos = 0.0f;
        }

        disp_pos = pos;
    }

    pthread_mutex_lock(&s->lock);
    if (playing) {
        s->play_pos = pos;
        s->external_play_pos = pos;   // keep UI coherent while playing
    } else {
        s->external_play_pos = pos;   // show scrub position when stopped
    }
    s->display_pos   = disp_pos;
    s->display_speed = disp_speed;
    s->display_amp   = disp_amp;
    pthread_mutex_unlock(&s->lock);
}


static void clamp_params(Player* state) {
    clampf(&state->scrub_target, 0.0f, (float)(state->num_frames - 1));
    clampf(&state->play_pos, 0.0f, (float)(state->num_frames - 1));
    clampf(&state->playback_speed, 0.1f, 4.0f);
    clampf(&state->amp, 0.0f, 1.0f);
}

static void player_draw_ui(Module* m, int y, int x) {
	Player* state = (Player*)m->state;

	pthread_mutex_lock(&state->lock);
	float pos = state->display_pos;
	float speed = state->display_speed;
	float amp = state->display_amp;
	float dur_sec = (float)state->num_frames / state->file_rate;
	float pos_sec = pos / state->file_rate;
	bool is_playing = state->playing;
	char cmd[64];
	strncpy(cmd, state->command_buffer, sizeof(cmd));
	cmd[sizeof(cmd) - 1] = '\0';
	pthread_mutex_unlock(&state->lock);

	BLUE();
	mvprintw(y,   x, "[Player:%s] ", m->name);
	CLR();

	LABEL(2, "");
	ORANGE(); printw(" %.2f s / %.2f s (%s) | ", pos_sec, dur_sec, is_playing ? "P" : "S"); CLR();

	LABEL(2, "speed:");
	ORANGE(); printw(" %.2fx | ", speed); CLR();

	LABEL(2, "amp:");
	ORANGE(); printw(" %.2f", amp); CLR();

	YELLOW();
	mvprintw(y+1, x, "Keys: -/= scrub | _/+ (speed) | [/] (amp) | p=play, s=stop"); 
	mvprintw(y+2, x, "Cmd: :1=pos :2=speed :3=amp"); 
	BLACK();
}

static void player_handle_input(Module* m, int key) {
    Player* state = (Player*)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
			case '-': state->play_pos -= state->sample_rate * 0.1f; handled = 1; break;
            case '=': state->play_pos += state->sample_rate * 0.1f; handled = 1; break;
            case '_': state->playback_speed -= 0.01f; handled = 1; break;
            case '+': state->playback_speed += 0.01f; handled = 1; break;
            case '[': state->amp -= 0.01f; handled = 1; break;
            case ']': state->amp += 0.01f; handled = 1; break;
            case 'p': state->playing = true;  handled = 1; break;
            case 's': state->playing = false; handled = 1; break;
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
            if (sscanf(state->command_buffer, "%c%f", &type, &val) == 2) {
                if (type == '1') {
                    float new_pos = val * state->sample_rate;
                    if (new_pos < 0) new_pos = 0;
                    if (new_pos > state->num_frames - 1) new_pos = state->num_frames - 1;
                    state->play_pos = new_pos;
                    state->external_play_pos = new_pos;
                } else if (type == '2') {
                    state->playback_speed = val;
                } else if (type == '3') {
					state->amp = val;
				}
            }
            state->command_index = 0;
            state->command_buffer[0] = '\0';
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

static void player_set_osc_param(Module* m, const char* param, float value) {
	Player* state = (Player*)m->state;
	pthread_mutex_lock(&state->lock);
	if (strcmp(param, "speed") == 0) {
		state->playback_speed = value;
	}
	if (strcmp(param, "amp") == 0) {
		state->amp = value;
	}
	clamp_params(state);
	pthread_mutex_unlock(&state->lock);
}

static void player_destroy(Module* m) {
	Player* state = (Player*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	char filepath[512] = "sample.wav";  // default

	// Parse args for file=
	if (args && strstr(args, "file=")) {
		const char* file_arg = strstr(args, "file=") + 5;
		// Copy until we hit a comma, space, or end of string
		size_t i = 0;
		while (*file_arg && *file_arg != ',' && *file_arg != ' ' && i < sizeof(filepath) - 1) {
			filepath[i++] = *file_arg++;
		}
	filepath[i] = '\0';
	}

	SF_INFO info = {0};
	SNDFILE* f = sf_open(filepath, SFM_READ, &info);
	if (!f) {
		fprintf(stderr, "[Player] Failed to open WAV file '%s' or file is not mono.\n", filepath);
		if (f) sf_close(f);
		return NULL;
	}

	float* raw = calloc(info.frames * info.channels, sizeof(float));
	sf_read_float(f, raw, info.frames * info.channels);

	float* data = calloc(info.frames, sizeof(float));
	for (sf_count_t i = 0; i < info.frames; i++) {
		float sum = 0.0f;
		for (int ch = 0; ch < info.channels; ch++) {
			sum += raw[i * info.channels + ch];
		}
		data[i] = sum / info.channels;  // average for mono
	}
	free(raw);

	sf_close(f);
	
	float playback_speed = 1.0f;
	float amp = 1.0f;

	if (args && strstr(args, "speed=")) {
        sscanf(strstr(args, "speed="), "speed=%f", &playback_speed);
    }
	if (args && strstr(args, "amp=")) {
        sscanf(strstr(args, "amp="), "amp=%f", &amp);
    }

	Player* state = calloc(1, sizeof(Player));
	state->sample_rate = sample_rate;
	state->file_rate = (float)info.samplerate;
	state->data = data;
	state->num_frames = info.frames;
	state->scrub_target = 0.0f;
	state->external_play_pos = 0.0f;
	state->play_pos = 0.0f;
	state->playback_speed = playback_speed;
	state->amp = amp;
	state->playing = true;
	pthread_mutex_init(&state->lock, NULL);
	init_smoother(&state->smooth_speed, 0.75f);
	init_smoother(&state->smooth_amp, 0.75f);
	clamp_params(state);

	Module* m = calloc(1, sizeof(Module));
	m->name = "player";
	m->state = state;
	m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->process = player_process;
	m->draw_ui = player_draw_ui;
	m->handle_input = player_handle_input;
	m->set_param = player_set_osc_param;
	m->destroy = player_destroy;
	return m;
}
