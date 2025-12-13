#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_function.h"
#include "module.h"
#include "util.h"

static void c_asr_process_control(Module* m) {
    CFunction* s = (CFunction*)m->state;

    float base_att, base_rel, base_depth;
    int   base_cycle;
    float threshold_trig, threshold_gate;
    int   short_mode;

    // 1) Snapshot base params
    pthread_mutex_lock(&s->lock);
    base_att       = s->attack_time;
    base_rel       = s->release_time;
    base_depth     = s->depth;
    base_cycle     = s->cycle;
    threshold_trig = s->threshold_trigger;
    threshold_gate = s->threshold_gate;
    short_mode     = s->short_mode;
    pthread_mutex_unlock(&s->lock);

    // 2) Smooth ONLY the base params (UI / OSC)
    float att   = process_smoother(&s->smooth_att,   base_att);
    float rel   = process_smoother(&s->smooth_rel,   base_rel);
    float depth = process_smoother(&s->smooth_depth, base_depth);

    int   cycle    = base_cycle;
    bool  trig_any = false;   // any trigger/gate crossing this block

    // 3) Control inputs: CV modulation on top of smoothed params
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j])
            continue;

        const char* param = m->control_input_params[j];
        float control     = *(m->control_inputs[j]);
        float norm        = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "att") == 0) {
            float max_att   = short_mode ? 10.0f   : 1000.0f;
            float mod_range = max_att - att;
            att += norm * mod_range;

        } else if (strcmp(param, "rel") == 0) {
            float max_rel   = short_mode ? 10.0f   : 1000.0f;
            float mod_range = max_rel - rel;
            rel += norm * mod_range;

        } else if (strcmp(param, "depth") == 0) {
            float mod_range = 1.0f - depth;
            depth += norm * mod_range;

        } else if (strcmp(param, "cycle") == 0) {
            cycle = (control > s->threshold_cycle);

        } else if (strcmp(param, "trig") == 0) {
            if (control > threshold_trig)
                trig_any = true;

        } else if (strcmp(param, "gate") == 0) {
            // For c_function, "gate" is just another trigger source (edge),
            // NOT a hold-sustain gate.
            if (control > threshold_gate)
                trig_any = true;
        }
    }

    // 4) Clamp final values (after CV)
    if (short_mode) {
        if (att < 0.01f) att = 0.01f;
        if (rel < 0.01f) rel = 0.01f;
        if (att > 10.0f) att = 10.0f;
        if (rel > 10.0f) rel = 10.0f;
    } else {
        if (att < 0.01f) att = 0.01f;
        if (rel < 0.01f) rel = 0.01f;
        // no upper bound, match clamp_params()
    }

    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;

    // 5) Update display values + cycle flag
    pthread_mutex_lock(&s->lock);
    s->display_att   = att;
    s->display_rel   = rel;
    s->display_depth = depth;
    s->cycle         = cycle;
    s->display_cycle = cycle;
    pthread_mutex_unlock(&s->lock);

    // 6) Trigger edge (control-rate)
    bool trig_prev = s->gate_prev;   // reused as "previous trigger state"
    bool trig_now  = trig_any;       // any source high this block

    if (trig_now && !trig_prev && s->state == ENV_IDLE) {
        // Start a new function from current level
        s->state              = ENV_ATTACK;
        s->timer              = 0.0f;
        s->attack_start_level = s->envelope_out;
    }
    s->gate_prev = trig_now;

    // 7) Per-sample envelope: ATTACK -> RELEASE -> (cycle or idle)
    float sr = s->sample_rate;
    if (sr <= 0.0f) sr = 48000.0f;

    for (unsigned long i = 0; i < MAX_BLOCK_SIZE; i++) {
        float step = 1.0f / sr;

        switch (s->state) {
            case ENV_ATTACK: {
                float step_size = 1.0f / att;     // seconds -> rate
                float delta     = step_size * step;

                s->envelope_out += delta;
                if (s->envelope_out >= 1.0f - 1e-4f) {
                    s->envelope_out = 1.0f;
                    s->timer        = 0.0f;

                    // Immediately go to RELEASE (no sustain in c_function)
                    s->release_start_level = s->envelope_out;
                    s->state               = ENV_RELEASE;
                }
                break;
            }

            case ENV_RELEASE: {
                float step_size = 1.0f / rel;
                float delta     = step_size * step;

                s->envelope_out -= delta;
                if (s->envelope_out <= 1e-4f) {
                    s->envelope_out = 0.0f;

                    if (s->cycle && !s->cycle_stop_requested) {
                        // loop: start attack again
                        s->state              = ENV_ATTACK;
                        s->timer              = 0.0f;
                        s->attack_start_level = s->envelope_out;
                    } else {
                        if (s->cycle_stop_requested) {
                            s->cycle = false;
                            s->cycle_stop_requested = false;
                        }
                        s->state = ENV_IDLE;
                    }
                }
                break;
            }

            case ENV_IDLE:
            default:
                // Free-running cycle if enabled and idle
                if (s->cycle && !s->cycle_stop_requested) {
                    s->state              = ENV_ATTACK;
                    s->timer              = 0.0f;
                    s->attack_start_level = s->envelope_out;
                } else {
                    s->envelope_out = 0.0f;
                }
                break;
        }

        float out = s->envelope_out * depth;
        m->control_output[i] = fminf(fmaxf(out, 0.0f), 1.0f);
    }
}

static void clamp_params(CFunction* s) {
    if (s->short_mode) {
        clampf(&s->attack_time, 0.01f, 10.0f);
        clampf(&s->release_time, 0.01f, 10.0f);
    } else {
        clampf(&s->attack_time, 0.01f, INFINITY);   // no upper bound
        clampf(&s->release_time, 0.01f, INFINITY);
    }

    clampf(&s->depth, 0.0f, 1.0f);
    clampf(&s->threshold_gate, 0.0f, 1.0f);
}


static void c_asr_draw_ui(Module* m, int y, int x) {
    CFunction* s = (CFunction*)m->state;
    pthread_mutex_lock(&s->lock);

	BLUE();
    mvprintw(y, x,   "[Function:%s] ", m->name);
	CLR();

	LABEL(2, "att:");
	ORANGE(); printw(" %.2fs | ", s->display_att); CLR();
	
	LABEL(2, "rel:");
	ORANGE(); printw(" %.2fs | ", s->display_rel); CLR();
	
	LABEL(2, "gate:");
	ORANGE(); printw(" %.1f | ", s->threshold_gate); CLR();

	LABEL(2, "depth:");
	ORANGE(); printw(" %.2f |", s->display_depth); CLR();

	ORANGE(); printw("%s|", s->short_mode ? "s" : "l"); CLR();

	ORANGE(); printw("%s", s->display_cycle ? "c" : "t"); CLR();

	YELLOW();
    mvprintw(y+1, x, "Keys: fire/cycle f/c, att -/=, rel _/+, gate [/], depth d/D, sh/lng [m]");
    mvprintw(y+2, x, "Command: :1 [att], :2 [rel], :3 [g_thresh], :d[depth]");
    pthread_mutex_unlock(&s->lock);
	BLACK();
}

static void c_asr_handle_input(Module* m, int key) {
    CFunction* s = (CFunction*)m->state;
    int handled = 0;
    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
			case 'f':  // toggle to triggered mode
				if (s->cycle) {
					s->cycle = false;
					s->display_cycle = false;
					s->cycle_stop_requested = true;
				} else if (s->state == ENV_IDLE) {
					s->state = ENV_ATTACK;
					s->timer = 0.0f;
				}
				s->trigger_held = true;
				handled = 1;
				break;
			case 'c':  // toggle to cycle mode
				if (!s->cycle) {
					s->cycle = true;
					s->display_cycle = true;
					s->cycle_stop_requested = false;
					s->trigger_held = true;
					if (s->state == ENV_IDLE) {
						s->state = ENV_ATTACK;
						s->timer = 0.0f;
					}
				} else {
					s->cycle_stop_requested = true;  // queue stop after release
					s->display_cycle = false;
				}
				handled = 1;
				break;

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
    CFunction* s = (CFunction*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "att") == 0) {
        s->attack_time = value * 1000.0f; 
    } else if (strcmp(param, "rel") == 0) {
        s->release_time = value * 1000.0f; 
    } else if (strcmp(param, "cycle") == 0) {
		if (value <= 0.5f && s->cycle) {
			s->cycle_stop_requested = true;
			s->display_cycle = false;
		} else if (value > 0.5f) {
			s->cycle = true;
			s->display_cycle = true;
		}
    } else if (value > 0.5f) {
        s->cycle = true;
    } else if (strcmp(param, "trig") == 0) {
        if (value > s->threshold_trigger && s->state == ENV_IDLE) {
            s->state = ENV_ATTACK;
            s->timer = 0;
        }
    } else if (strcmp(param, "depth") == 0) {
		s->depth = value;
    } else if (strcmp(param, "gate") == 0) {
		s->threshold_gate = value;
	}
    pthread_mutex_unlock(&s->lock);
}


static void c_asr_destroy(Module* m) {
    CFunction* state = (CFunction*)m->state;
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

    CFunction* s = calloc(1, sizeof(CFunction));
    s->attack_time = attack_time;
    s->release_time = release_time;
    s->envelope_out = 0.0f;
	s->depth = depth;
	s->gate_prev = false;
    s->sample_rate = sample_rate;
    s->short_mode = true;
    s->threshold_trigger = 0.5f;
    s->threshold_cycle = 0.5f;
	s->threshold_gate = 0.5f;
    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_att, 0.75f);
    init_smoother(&s->smooth_rel, 0.75f);
	init_smoother(&s->smooth_depth, 0.75f);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_function";
    m->state = s;
    m->process_control = c_asr_process_control;
    m->draw_ui = c_asr_draw_ui;
    m->handle_input = c_asr_handle_input;
    m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
	m->set_param = c_asr_set_osc_param;
    m->destroy = c_asr_destroy;
    return m;
}
