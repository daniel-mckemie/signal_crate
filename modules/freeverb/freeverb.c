#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "freeverb.h"
#include "module.h"
#include "util.h"

// Delay lengths (prime-ish for decorrelation), must be < MAX_DELAY
static const int comb_lengths[NUM_COMBS] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static const int allpass_lengths[NUM_ALLPASS] = {556, 441, 341, 225};

static void delayline_init(DelayLine* d, int size) {
    d->size = size;
    d->index = 0;
    memset(d->buffer, 0, sizeof(float) * MAX_DELAY);
}

static float delayline_process(DelayLine* d, float input) {
    float out = d->buffer[d->index];
    d->buffer[d->index] = input;
    d->index = (d->index + 1) % d->size;
    return out;
}

static float allpass_process(DelayLine* d, float input) {
    int idx = d->index;
    float bufout = d->buffer[idx];
    float output = -input + bufout;
    d->buffer[idx] = input + (bufout * 0.5f);  // 0.5 is the standard Freeverb allpass gain
    d->index = (idx + 1) % d->size;
    return output;
}

static void freeverb_process(Module* m, float* in, unsigned long frames) {
    Freeverb* s = (Freeverb*)m->state;

    float fb = process_smoother(&s->smooth_feedback, s->feedback);
    float damp = process_smoother(&s->smooth_damping, s->damping);
    float wet = process_smoother(&s->smooth_wet, s->wet);

	float mod_depth = 1.0f;
	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);
		if (strcmp(param, "fb") == 0) {
			float mod_range = s->feedback * mod_depth;
			fb = s->feedback + norm * mod_range;
		} else if (strcmp(param, "damp") == 0) {
			float mod_range = (1.0f - s->damping) * mod_depth;
			damp = s->damping + norm * mod_range;
		} else if (strcmp(param, "wet") == 0) {
			float mod_range = (1.0f - s->wet) * mod_depth;
			wet = s->wet + norm * mod_range;
		}
	}

    s->display_feedback = fb;
    s->display_damping = damp;
    s->display_wet = wet;

    for (unsigned long i = 0; i < frames; i++) {
        float input = in[i];
        float acc = 0.0f;

        for (int j = 0; j < NUM_COMBS; j++) {
            float out = delayline_process(&s->combs[j], input + s->comb_filterstore[j] * fb);
            s->comb_filterstore[j] = damp * s->comb_filterstore[j] + (1 - damp) * out;
            acc += out;
        }

        float allpass_out = acc;
		for (int j = 0; j < NUM_ALLPASS; j++) {
		    allpass_out = allpass_process(&s->allpasses[j], allpass_out);
		}


		float dry = 1.0f - wet;
        m->output_buffer[i] = dry * input + wet * (allpass_out / NUM_COMBS);
    }
}

static void clamp_params(Freeverb* s) {
    clampf(&s->feedback, 0.0f, 0.99f);
    clampf(&s->damping, 0.0f, 1.0f);
    clampf(&s->wet, 0.0f, 1.0f);
}

static void freeverb_draw_ui(Module* m, int y, int x) {
    Freeverb* s = (Freeverb*)m->state;
	float fb, damp, wet;

	pthread_mutex_lock(&s->lock);
	fb = s->display_feedback;
	damp = s->display_damping;
	wet = s->display_wet;
    pthread_mutex_unlock(&s->lock);

    mvprintw(y, x, "[Freeverb:%s] fb: %.2f | damp: %.2f | wet: %.2f", m->name, fb, damp, wet);
    mvprintw(y+1, x, "Keys: -/= fb, _/+ damp, [/] wet");
    mvprintw(y+2, x, "Cmd: :1 [fb], :2 [damp], :3 [wet]");
}

static void freeverb_handle_input(Module* m, int key) {
    Freeverb* s = (Freeverb*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '-': s->feedback -= 0.01f; handled = 1; break;
            case '=': s->feedback += 0.01f; handled = 1; break;
            case '_': s->damping -= 0.01f; handled = 1; break;
            case '+': s->damping += 0.01f; handled = 1; break;
            case '[': s->wet -= 0.01f; handled = 1; break;
            case ']': s->wet += 0.01f; handled = 1; break;
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
                if (type == '1') s->feedback = val;
                else if (type == '2') s->damping = val;
                else if (type == '3') s->wet = val;
            }
            handled = 1;
        } else if (key == 27) {
            s->entering_command = false;
            handled = 1;
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

static void freeverb_set_param(Module* m, const char* param, float value) {
    Freeverb* s = (Freeverb*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "fb") == 0) s->feedback = value;
    else if (strcmp(param, "damp") == 0) s->damping = value;
    else if (strcmp(param, "wet") == 0) s->wet = value;
    else fprintf(stderr, "[freeverb] Unknown OSC param: %s\n", param);

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void freeverb_destroy(Module* m) {
    if (!m) return;
    Freeverb* s = (Freeverb*)m->state;
    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float fb = 0.5f, damp = 0.5f, wet = 0.33f;

    if (args && strstr(args, "fb=")) sscanf(strstr(args, "fb="), "fb=%f", &fb);
    if (args && strstr(args, "damp=")) sscanf(strstr(args, "damp="), "damp=%f", &damp);
    if (args && strstr(args, "wet=")) sscanf(strstr(args, "wet="), "wet=%f", &wet);

    Freeverb* s = calloc(1, sizeof(Freeverb));
	memset(s->comb_filterstore, 0, sizeof(float) * NUM_COMBS);
    s->sample_rate = sample_rate;
    s->feedback = fb;
    s->damping = damp;
    s->wet = wet;

    for (int i = 0; i < NUM_COMBS; i++) delayline_init(&s->combs[i], comb_lengths[i]);
    for (int i = 0; i < NUM_ALLPASS; i++) delayline_init(&s->allpasses[i], allpass_lengths[i]);

    init_smoother(&s->smooth_feedback, 0.5f);
    init_smoother(&s->smooth_damping, 0.5f);
    init_smoother(&s->smooth_wet, 0.5f);

    pthread_mutex_init(&s->lock, NULL);
    clamp_params(s);

    Module* m = calloc(1, sizeof(Module));
    m->name = "freeverb";
    m->state = s;
    m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = freeverb_process;
    m->draw_ui = freeverb_draw_ui;
    m->handle_input = freeverb_handle_input;
    m->set_param = freeverb_set_param;
    m->destroy = freeverb_destroy;
    return m;
}

