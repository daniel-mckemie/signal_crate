#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_asr.h"
#include "module.h"
#include "util.h"

static void c_asr_process_control(Module* m) {
    CASR* s = (CASR*)m->state;

    float att, sus, rel, depth;

    // Read base params
    pthread_mutex_lock(&s->lock);
    att   = s->attack_time;
    sus   = s->sustain_level;
    rel   = s->release_time;
    depth = s->depth;
    pthread_mutex_unlock(&s->lock);

    float* gate_buf = NULL;
    float mod_depth = 1.0f;

    // --- modulation inputs ---
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j])
            continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
        float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "att") == 0) {
            float mod_range = (s->short_mode ? 10.0f - att : 1000.0f - att) * mod_depth;
            att += norm * mod_range;

        } else if (strcmp(param, "rel") == 0) {
            float mod_range = (s->short_mode ? 10.0f - rel : 1000.0f - rel) * mod_depth;
            rel += norm * mod_range;

        } else if (strcmp(param, "depth") == 0) {
            float mod_range = (1.0f - s->depth) * mod_depth;
            depth = s->depth + norm * mod_range;

        } else if (strcmp(param, "gate") == 0) {
            gate_buf = m->control_inputs[j];
        }
    }

    // Clamp + smooth
    sus   = fminf(fmaxf(sus,   0.01f), 1.0f);
    depth = fminf(fmaxf(depth, 0.0f),  1.0f);

    att   = process_smoother(&s->smooth_att,   att);
    sus   = process_smoother(&s->smooth_sus,   sus);
    rel   = process_smoother(&s->smooth_rel,   rel);
    depth = process_smoother(&s->smooth_depth, depth);

    // Write display params
    pthread_mutex_lock(&s->lock);
    s->display_att   = att;
    s->display_sus   = sus;
    s->display_rel   = rel;
    s->display_depth = depth;
    pthread_mutex_unlock(&s->lock);

    // Envelope loop
    bool prev_gate = s->gate_prev;
    float sr = s->sample_rate;

    for (unsigned long i = 0; i < MAX_BLOCK_SIZE; i++) {
        float gate_sample = (gate_buf ? gate_buf[i] : 0.0f);
        bool gate_now = (gate_sample >= s->threshold_gate);

        // Rising edge starts attack
        if (gate_now && !prev_gate) {
            s->state = ENV_ATTACK;
        }

        float step = 1.0f / sr;

        switch (s->state) {

        case ENV_ATTACK: {
            float inc = step / fmaxf(att, 0.001f);
            s->envelope_out += inc;
            if (s->envelope_out >= 1.0f) {
                s->envelope_out = 1.0f;
                s->state = ENV_SUSTAIN;
            }
            break;
        }

        case ENV_SUSTAIN:
            s->envelope_out = sus;
            if (!gate_now) {
                s->state = ENV_RELEASE;
            }
            break;

        case ENV_RELEASE: {
            float dec = step / fmaxf(rel, 0.001f);
            s->envelope_out -= dec;
            if (s->envelope_out <= 0.0f) {
                s->envelope_out = 0.0f;
                s->state = ENV_IDLE;
            }
            break;
        }

        case ENV_IDLE:
        default:
            // stay at zero; only rising gate restarts
            s->envelope_out = 0.0f;
            break;
        }

        m->control_output[i] = s->envelope_out * depth;
        prev_gate = gate_now;
    }

    s->gate_prev = prev_gate;
}

static void clamp_params(CASR* s) {
    if (s->short_mode) {
        clampf(&s->attack_time, 0.01f, 10.0f);
        clampf(&s->release_time, 0.01f, 10.0f);
    } else {
        clampf(&s->attack_time, 0.01f, INFINITY);   // no upper bound
        clampf(&s->release_time, 0.01f, INFINITY);
    }

    clampf(&s->sustain_level, 0.01f, 1.0f);
    clampf(&s->depth, 0.0f, 1.0f);
    clampf(&s->threshold_gate, 0.0f, 1.0f);
}


static void c_asr_draw_ui(Module* m, int y, int x) {
    CASR* s = (CASR*)m->state;
    pthread_mutex_lock(&s->lock);

	BLUE();
    mvprintw(y, x,   "[ASR:%s] ", m->name);
	CLR();

	LABEL(2, "att:");
	ORANGE(); printw(" %.2fs | ", s->display_att); CLR();
	
	LABEL(2, "rel:");
	ORANGE(); printw(" %.2fs | ", s->display_rel); CLR();
	
	LABEL(2, "gate:");
	ORANGE(); printw(" %.2f | ", s->threshold_gate); CLR();

	LABEL(2, "depth:");
	ORANGE(); printw(" %.2f | ", s->display_depth); CLR();

	ORANGE(); printw("%s", s->short_mode ? "s" : "l"); CLR();

	YELLOW();
    mvprintw(y+1, x, "Keys: att -/=, rel _/+, gate [/], dpth d/D, sh/lng [m]");
    mvprintw(y+2, x, "Command: :1 [att], :2 [rel], :3 [g_thresh], :d[depth]");
    pthread_mutex_unlock(&s->lock);
	BLACK();
}

static void c_asr_handle_input(Module* m, int key) {
    CASR* s = (CASR*)m->state;
    int handled = 0;
    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
			case 'm': s->short_mode = !s->short_mode; handled = 1; break;
            case '-': s->attack_time -= 0.1f; handled = 1; break;
            case '=': s->attack_time += 0.1f; handled = 1; break;
            case '_': s->release_time -= 0.1f; handled = 1; break;
            case '+': s->release_time += 0.1f; handled = 1; break;
            case '[': s->threshold_gate -= 0.1f; handled = 1; break;
            case ']': s->threshold_gate += 0.1f; handled = 1; break;
            case 'd': s->depth -= 0.01f; handled = 1; break;
            case 'D': s->depth += 0.01f; handled = 1; break;
            case ':':
                s->entering_command = true;
                memset(s->command_buffer, 0, sizeof(s->command_buffer));
                s->command_index = 0;
                handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            s->entering_command = false;
            char type;
            float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') s->attack_time = val;
				else if (type == '2') s->release_time = val;
				else if (type == '3') s->threshold_gate = val;
				else if (type == 'd') s->depth = val;
            }
            handled = 1;
        } else if (key == 27) {
            s->entering_command = false; handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 && s->command_index < sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }


    if (handled) clamp_params(s); 
	pthread_mutex_unlock(&s->lock);
}


static void c_asr_set_osc_param(Module* m, const char* param, float value) {
    CASR* s = (CASR*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "att") == 0) {
        s->attack_time = value * 1000.0f; 
    } else if (strcmp(param, "rel") == 0) {
        s->release_time = value * 1000.0f; 
    } else if (strcmp(param, "depth") == 0) {
		s->depth = value;
    } else if (strcmp(param, "gate") == 0) {
		s->threshold_gate = value;
	}
    pthread_mutex_unlock(&s->lock);
}


static void c_asr_destroy(Module* m) {
    CASR* state = (CASR*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
	float attack_time = 1.0f;
    float release_time = 1.0f;
	float depth = 0.5f;

	if (args && strstr(args, "att=")) {
        sscanf(strstr(args, "att="), "att=%f", &attack_time);
    }
    if (args && strstr(args, "rel=")) {
        sscanf(strstr(args, "rel="), "rel=%f", &release_time);
	}
	if (args && strstr(args, "depth=")) {
        sscanf(strstr(args, "depth="), "depth=%f", &depth);
    }

    CASR* s = calloc(1, sizeof(CASR));
    s->attack_time = attack_time;
    s->release_time = release_time;
    s->sustain_level = 1.0f;
    s->envelope_out = 0.0f;
	s->depth = depth;
	s->gate_prev = false;
    s->sample_rate = sample_rate;
    s->short_mode = true;
	s->threshold_gate = 0.5f;
    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_att, 0.75f);
    init_smoother(&s->smooth_rel, 0.75f);
    init_smoother(&s->smooth_sus, 0.75f);
	init_smoother(&s->smooth_depth, 0.75f);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_asr";
    m->state = s;
    m->process_control = c_asr_process_control;
    m->draw_ui = c_asr_draw_ui;
    m->handle_input = c_asr_handle_input;
    m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->set_param = c_asr_set_osc_param;
    m->destroy = c_asr_destroy;
    return m;
}
