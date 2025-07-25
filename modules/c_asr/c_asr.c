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

    // Step 1: Lock and read base values
    pthread_mutex_lock(&s->lock);
    att = s->attack_time;
    sus = s->sustain_level;
    rel = s->release_time;
	depth = s->depth; 
    pthread_mutex_unlock(&s->lock);

    // Step 2: Modulate with control inputs
    float mod_depth = 1.0f;
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);
        float norm = fminf(fmaxf(control, -1.0f), 1.0f);

        if (strcmp(param, "trig") == 0) {
            s->trigger_held = (control > s->threshold_trigger);
            if (s->trigger_held && s->state == ENV_IDLE) {
                s->state = ENV_ATTACK;
                s->timer = 0.0f;
            }
        } else if (strcmp(param, "cycle") == 0) {
            s->cycle = (control > s->threshold_cycle);
        } else if (strcmp(param, "att") == 0) {
			float mod_range;
			if (s->short_mode) {
				mod_range = (10.0f - att) * mod_depth;
			} else {
				mod_range = (1000.0f - att) * mod_depth;
			}
			att = att + norm * mod_range;
        } else if (strcmp(param, "rel") == 0) {
			float mod_range;
			if (s->short_mode) {
				mod_range = (10.0f - rel) * mod_depth;
			} else {
				mod_range = (1000.0f - rel) * mod_depth;
			}
            rel = rel + norm * mod_range;
        } else if (strcmp(param, "depth") == 0) {
            float mod_range = (1.0f - s->depth) * mod_depth;
            depth = s->depth + norm * mod_range;
		}
    }

	sus = fminf(fmaxf(sus, 0.01f), 1.0f);
	depth = fminf(fmaxf(depth, 0.0f), 1.0f);

    // Step 3: Smooth now, with safe variables
    att = process_smoother(&s->smooth_att, att);
    sus = process_smoother(&s->smooth_sus, sus);
    rel = process_smoother(&s->smooth_rel, rel);
	depth = process_smoother(&s->smooth_depth, depth);

    // Step 4: Write smoothed and display values back under lock
    pthread_mutex_lock(&s->lock);
    s->display_att = att;
    s->display_sus = sus;
    s->display_rel = rel;
	s->display_depth = depth;
    pthread_mutex_unlock(&s->lock);

    // Step 5: Envelope
    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++) {
		float step = 1.0f / s->sample_rate;
		float sustain = sus;

		switch (s->state) {
			case ENV_ATTACK: {
				float step_size = 1.0f / fmaxf(att, 0.001f);
				float delta = step_size * step;
				s->envelope_out += delta;
				s->envelope_out = fminf(s->envelope_out, 1.0f);
				s->timer += step;

				if (s->envelope_out >= 1.0f - 1e-4f) {
					s->envelope_out = 1.0f;
					s->state = ENV_SUSTAIN;
					s->timer = 0.0f;
				}
				break;
			}
			
			case ENV_SUSTAIN:
				s->envelope_out = sustain;
				if (!s->cycle || !s->trigger_held) {
					s->release_start_level = s->envelope_out;
					s->state = ENV_RELEASE;
					s->timer = 0.0f;
					s->trigger_held = false;
				}
				break;

			case ENV_RELEASE: {
				float step_size = 1.0f / fmaxf(rel, 0.001f); 
				float delta = step_size * step;
				s->envelope_out -= delta;
				s->envelope_out = fmaxf(s->envelope_out, 0.0f);
				s->timer += step;

				if (s->envelope_out <= 1e-4f) {
					s->envelope_out = 0.0f;
					if (s->cycle_stop_requested) {
						s->cycle = false;
						s->cycle_stop_requested = false;
						s->state = ENV_IDLE;
					} else if (s->cycle && s->trigger_held) {
						s->state = ENV_ATTACK;
						s->timer = 0.0f;
					} else {
						s->state = ENV_IDLE;
					}
				}
				break;
			}
						
			case ENV_IDLE:
			default:
				if (s->cycle) {
					s->state = ENV_ATTACK;
					s->timer = 0.0f;
				} else {
					s->envelope_out = 0.0f;
				}
				break;
		}

		float out = s->envelope_out * depth;
		m->control_output[i] = fminf(fmaxf(out, 0.0f), 1.0f);
	}

}

static void c_asr_draw_ui(Module* m, int y, int x) {
    CASR* s = (CASR*)m->state;
    pthread_mutex_lock(&s->lock);
    mvprintw(y, x,   "[ASR:%s] att: %.2fs | rel: %.2fs | depth: %.2f | %s | %s", m->name, s->display_att, s->display_rel, s->display_depth, s->short_mode ? "short" : "long", s->display_cycle ? "cyc" : "trig");
    mvprintw(y+1, x, "Keys: t = trig, c = cycle, :att -/=, :rel _/+, :d/D [depth]");
    mvprintw(y+2, x, "Command: :1 [att], :2 [rel], :d[depth], :l [long/short]");
    pthread_mutex_unlock(&s->lock);
}

static void clamp_params(CASR* s) {
    if (s->short_mode) {
        if (s->attack_time < 0.01f) s->attack_time = 0.01f;
        if (s->attack_time > 10.0f)  s->attack_time = 10.0f;

        if (s->release_time < 0.01f) s->release_time = 0.01f;
        if (s->release_time > 10.0f)  s->release_time = 10.0f;
    } else {
		// No upper bounds
        if (s->attack_time < 0.01f) s->attack_time = 0.01f;
        if (s->release_time < 0.01f) s->release_time = 0.01f;
    }

    if (s->sustain_level < 0.01f) s->sustain_level = 0.01f;
    if (s->sustain_level > 1.0f)  s->sustain_level = 1.0f;

    if (s->depth < 0.0f)  s->depth = 0.0f;
    if (s->depth > 1.0f)  s->depth = 1.0f;
}

static void c_asr_handle_input(Module* m, int key) {
    CASR* s = (CASR*)m->state;
    int handled = 0;
    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
			case 't':
			    if (s->state == ENV_IDLE) {
				s->state = ENV_ATTACK;
				s->timer = 0.0f;
			}
			s->trigger_held = true;
			handled = 1;
			break;
			case 'c':
				if (s->cycle) {
					s->cycle_stop_requested = true;  // queue stop
					s->display_cycle = false;
				} else {
					s->cycle = true;
					s->display_cycle = true;
				}
				handled = 1;
				break;

            case 'l': s->short_mode = !s->short_mode; handled = 1; break;
            case '-': s->attack_time -= 0.1f; handled = 1; break;
            case '=': s->attack_time += 0.1f; handled = 1; break;
            case '_': s->release_time -= 0.1f; handled = 1; break;
            case '+': s->release_time += 0.1f; handled = 1; break;
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
	}
    pthread_mutex_unlock(&s->lock);
}


static void c_asr_destroy(Module* m) {
    CASR* state = (CASR*)m->state;
	if (state) pthread_mutex_destroy(&state->lock);
    destroy_base_module(m);
}

Module* create_module(float sample_rate) {
    CASR* s = calloc(1, sizeof(CASR));
    s->attack_time = 1.0f;
    s->release_time = 1.0f;
    s->sustain_level = 1.0f;
    s->envelope_out = 0.0f;
	s->depth = 0.5f;
    s->sample_rate = sample_rate;
    s->short_mode = true;
    s->threshold_trigger = 0.5f;
    s->threshold_cycle = 0.5f;
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
    m->control_output = calloc(FRAMES_PER_BUFFER, sizeof(float));
	m->set_param = c_asr_set_osc_param;
    m->destroy = c_asr_destroy;
    return m;
}
