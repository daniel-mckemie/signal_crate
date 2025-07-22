#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_asr.h"
#include "module.h"
#include "util.h"

#define MIN_TIME 0.01f
#define MAX_TIME_SHORT 10.0f
#define MAX_TIME_LONG 100.0f

static void advance_envelope(CASR* s, float base) {
    float step = 1.0f / s->sample_rate;
    float attack = process_smoother(&s->smooth_att, s->attack_time);
    float release = process_smoother(&s->smooth_rel, s->release_time);
    float sustain = process_smoother(&s->smooth_sus, s->sustain_level);

    for (;;) {
        switch (s->state) {
            case ENV_ATTACK:
                s->timer += step;
                s->envelope_out = base + (s->peak_level - base) * fminf(1.0f, s->timer / attack);
                if (s->timer >= attack) {
                    s->state = ENV_SUSTAIN;
                    s->timer = 0.0f;
                    continue;
                }
                return;

            case ENV_SUSTAIN:
                s->envelope_out = base + (s->peak_level - base) * sustain;
                if (!s->cycle) {
                    s->state = ENV_RELEASE;
                    s->timer = 0.0f;
                    continue;
                }
                if (!s->trigger_held) {
                    s->state = ENV_RELEASE;
                    s->timer = 0.0f;
                    continue;
                }
                return;

            case ENV_RELEASE: {
                s->timer += step;
                float relval = fmaxf(0.0f, sustain * (1.0f - s->timer / release));
                s->envelope_out = base + (s->peak_level - base) * relval;
                if (relval <= 0.0f) {
                    if (s->cycle_stop_requested) {
                        s->cycle = false;
                        s->cycle_stop_requested = false;
                        s->state = ENV_IDLE;
                        s->envelope_out = base;
                        return;
                    } else if (s->cycle) {
                        s->state = ENV_ATTACK;
                        s->timer = 0.0f;
                        continue;
                    } else {
                        s->state = ENV_IDLE;
                        s->envelope_out = base;
                        return;
                    }
                }
                return;
            }

            case ENV_IDLE:
            default:
                if (s->cycle) {
                    s->state = ENV_ATTACK;
                    s->timer = 0.0f;
                    continue;
                } else {
                    s->envelope_out = base;  // idle/bypass
                    return;
                }
        }
    }
}

static void clamp_params(CASR* s) {
    float max_time = s->short_mode ? MAX_TIME_SHORT : MAX_TIME_LONG;
    s->attack_time = fminf(fmaxf(s->attack_time, MIN_TIME), max_time);
    s->release_time = fminf(fmaxf(s->release_time, MIN_TIME), max_time);
    s->sustain_level = fminf(fmaxf(s->sustain_level, 0.0f), 1.0f);
}

static void c_asr_process_control(Module* m) {
    CASR* s = (CASR*)m->state;
    pthread_mutex_lock(&s->lock);

	float base = s->base_level;
    for (int j = 0; j < m->num_control_inputs; j++) {
        if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

        const char* param = m->control_input_params[j];
        float control = *(m->control_inputs[j]);

        if (strcmp(param, "trig") == 0) { 
			s->trigger_held = (control > s->threshold_trigger);
			if (s->trigger_held && s->state == ENV_IDLE) {
				s->state = ENV_ATTACK;
			}
		}
        else if (strcmp(param, "cycle") == 0)
            s->cycle = control > s->threshold_cycle;
        else if (strcmp(param, "att") == 0)
            s->attack_time = MIN_TIME * powf((s->short_mode ? MAX_TIME_SHORT : MAX_TIME_LONG) / MIN_TIME, control);
        else if (strcmp(param, "rel") == 0)
            s->release_time = MIN_TIME * powf((s->short_mode ? MAX_TIME_SHORT : MAX_TIME_LONG) / MIN_TIME, control);
        else if (strcmp(param, "sus") == 0)
            s->sustain_level = control;
		else if (strcmp(param, "base") == 0) {
			base = control;
			s->base_level = base;
		}
		else if (strcmp(param, "peak") == 0)
			s->peak_level = control;
    }

    for (unsigned long i = 0; i < FRAMES_PER_BUFFER; i++) {
		for (int j = 0; j < m->num_control_inputs; j++) {
		    if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			if (strcmp(param, "base") == 0) {
				base = m->control_inputs[j][i];
				break;
			}
		}
        advance_envelope(s,base);
		
		if (s->state == ENV_IDLE) {
			m->control_output[i] = base;
		} else {
			m->control_output[i] = s->envelope_out;
		}
    }

    pthread_mutex_unlock(&s->lock);
}

static void c_asr_draw_ui(Module* m, int y, int x) {
    CASR* s = (CASR*)m->state;
    pthread_mutex_lock(&s->lock);
    mvprintw(y, x,   "[ASR] att: %.2fs sus %.2f rel: %.2fs base: %.2fs peak: %.2fs %s", s->attack_time, s->sustain_level, s->release_time, s->base_level, s->peak_level, s->cycle ? "Cycle" : "One-shot");
    mvprintw(y+1, x, "Keys: t = trig, c = cycle, l = range, :att -/=, :sus _/+, :rel [/]");
    mvprintw(y+2, x, "Command: :1 [att], :2 [sus], :3 [rel], :4 [base] :5 [peak]");
    pthread_mutex_unlock(&s->lock);
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
					s->timer = 0;
				}
				s->trigger_held = false;
				handled = 1;
				break;
			case 'c':
			if (s->cycle) {
				s->cycle_stop_requested = true;  // queue stop
			} else {
				s->cycle = true;
			}
			handled = 1;
			break;

            case 'l': s->short_mode = !s->short_mode; handled = 1; break;
            case '-': s->attack_time -= 0.1f; handled = 1; break;
            case '=': s->attack_time += 0.1f; handled = 1; break;
            case '_': s->sustain_level -= 0.1f; handled = 1; break;
            case '+': s->sustain_level += 0.1f; handled = 1; break;
            case '[': s->release_time -= 0.1f; handled = 1; break;
            case ']': s->release_time += 0.1f; handled = 1; break;
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
                else if (type == '2') s->sustain_level = val;
				else if (type == '3') s->release_time = val;
                else if (type == '4') s->base_level = val;
                else if (type == '5') s->base_level = val;
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
        s->attack_time = MIN_TIME * powf((s->short_mode ? MAX_TIME_SHORT : MAX_TIME_LONG) / MIN_TIME, value);
    } else if (strcmp(param, "rel") == 0) {
        s->release_time = MIN_TIME * powf((s->short_mode ? MAX_TIME_SHORT : MAX_TIME_LONG) / MIN_TIME, value);
    } else if (strcmp(param, "sus") == 0) {
        s->sustain_level = fminf(fmaxf(value, 0.0f), 1.0f);
    } else if (strcmp(param, "cycle") == 0) {
		if (value <= 0.5f && s->cycle) {
			s->cycle_stop_requested = true;
		}
    } else if (value > 0.5f) {
        s->cycle = true;
    } else if (strcmp(param, "trig") == 0) {
        if (value > s->threshold_trigger && s->state == ENV_IDLE) {
            s->state = ENV_ATTACK;
            s->timer = 0;
        }
    }
    pthread_mutex_unlock(&s->lock);
}


static void c_asr_destroy(Module* m) {
    CASR* s = (CASR*)m->state;
    if (s) {
        pthread_mutex_destroy(&s->lock);
        free(s);
    }
    if (m->control_output) free(m->control_output);
}

Module* create_module(float sample_rate) {
    CASR* s = calloc(1, sizeof(CASR));
    s->attack_time = 1.0f;
    s->release_time = 1.5f;
    s->sustain_level = 1.0f;
	s->base_level = 0.2f;
	s->peak_level = 0.8f;
    s->envelope_out = 0.0f;
    s->sample_rate = sample_rate;
    s->short_mode = true;
    s->threshold_trigger = 0.5f;
    s->threshold_cycle = 0.5f;
    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_att, 0.75f);
    init_smoother(&s->smooth_rel, 0.75f);
    init_smoother(&s->smooth_sus, 0.75f);

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
