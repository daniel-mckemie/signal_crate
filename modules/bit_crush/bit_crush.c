#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "module.h"
#include "util.h"
#include "bit_crush.h"

static void bit_crush_process(Module* m, float* in, unsigned long frames)
{
    BitCrushState* s = (BitCrushState*)m->state;
    float* out = m->output_buffer;
    float* input = (m->num_inputs > 0) ? m->inputs[0] : in;

    pthread_mutex_lock(&s->lock);
    float base_bits = s->bits;
    float base_rate = s->rate;
    pthread_mutex_unlock(&s->lock);

    float bits  = process_smoother(&s->smooth_bits, base_bits);
    float rate  = process_smoother(&s->smooth_rate, base_rate);
    float step  = rate / s->sample_rate;

    if (step <= 0.0f) step = 1.0f;

    float bit_levels = powf(2.0f, bits) - 1.0f;
    float phs = s->phase;
    float held = s->last_sample;

	for (int i = 0; i < m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param = m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

		if (strcmp(param, "rate") == 0) {
			float mod_depth = 1.0f;
			float mod_range = s->rate * mod_depth;
			rate = s->rate + norm * mod_range;
		}
		else if (strcmp(param, "bits") == 0) {
			float mod_depth = 1.0f;
			float mod_range = (16.0f - 2.0f) * mod_depth;
			bits = s->bits + norm * mod_range;
		}
	}
	clampf(&rate, 20.0f, s->sample_rate * 0.45f);
	clampf(&bits, 2.0f, 16.0f);

    for (unsigned long i = 0; i < frames; i++) {
        phs += step;
        if (phs >= 1.0f) {
            phs -= 1.0f;
            float inp = input ? input[i] : 0.0f;
            float quant = roundf(inp * bit_levels) / bit_levels;
            held = quant;
        }
        out[i] = held;
    }

    s->phase = phs;
    s->last_sample = held;
    s->display_bits = bits;
    s->display_rate = rate;
}

static void bit_crush_draw_ui(Module* m, int y, int x)
{
    BitCrushState* s = (BitCrushState*)m->state;
    pthread_mutex_lock(&s->lock);
    float bits = s->display_bits;
    float rate = s->display_rate;
    pthread_mutex_unlock(&s->lock);

    mvprintw(y,   x, "[BITCRUSH:%s] bits: %.0f | rate: %.1f Hz", m->name, bits, rate);
    mvprintw(y+1, x, "Keys: -/= bits, _/+ rate");
    mvprintw(y+2, x, "Command: :1 [bits], :2 [rate]");
}

static void clamp_params(BitCrushState* s)
{
	float min_rate = 20.0f;
    float max_rate = s->sample_rate * 0.45f;

    clampf(&s->bits, 2.0f, 16.0f);
    clampf(&s->rate, min_rate, max_rate);
}

static void bit_crush_handle_input(Module* m, int key)
{
    BitCrushState* s = (BitCrushState*)m->state;
    int handled = 0;
    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '-': s->bits -= 1.0f; handled = 1; break;
            case '=': s->bits += 1.0f; handled = 1; break;
            case '_': s->rate -= 0.5f; handled = 1; break;
            case '+': s->rate += 0.5f; handled = 1; break;
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
            char type; float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') s->bits = val;
                else if (type == '2') s->rate = val;
            }
            handled = 1;
        } else if (key == 27) {
            s->entering_command = false; handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 && s->command_index < sizeof(s->command_buffer)-1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled) clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void bit_crush_set_osc_param(Module* m, const char* param, float value) {
    BitCrushState* s = (BitCrushState*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "rate") == 0) {
        // Expect 0.0–1.0 from slider, map exponentially from 20 Hz to sample_rate * 0.45
        float min_rate = 20.0f;
        float max_rate = s->sample_rate * 0.45f;

        float norm = fminf(fmaxf(value, 0.0f), 1.0f);  // clamp 0–1
        float hz = min_rate * powf(max_rate / min_rate, norm);
        s->rate = hz;

    } else if (strcmp(param, "bits") == 0) {
        // Expect 0.0–1.0 from slider, map linearly from 1 to 16 bits
        float min_bits = 2.0f;
        float max_bits = 16.0f;
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        s->bits = min_bits + (max_bits - min_bits) * norm;

    } else {
        fprintf(stderr, "[bit_crush] Unknown OSC param: %s\n", param);
    }

    pthread_mutex_unlock(&s->lock);
}


static void bit_crush_destroy(Module* m)
{
    BitCrushState* s = (BitCrushState*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate)
{
    float bits = 8.0f;
    float rate = 8000.0f;

    if (args && strstr(args, "bits=")) sscanf(strstr(args, "bits="), "bits=%f", &bits);
    if (args && strstr(args, "rate=")) sscanf(strstr(args, "rate="), "rate=%f", &rate);

    BitCrushState* s = calloc(1, sizeof(BitCrushState));
    s->bits = bits;
    s->rate = rate;
    s->phase = 0.0f;
    s->last_sample = 0.0f;
	s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_bits, 0.75f);
    init_smoother(&s->smooth_rate, 0.75f);

    Module* m = calloc(1, sizeof(Module));
    m->name = "bit_crush";
    m->state = s;
    m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = bit_crush_process;
    m->draw_ui = bit_crush_draw_ui;
    m->handle_input = bit_crush_handle_input;
    m->set_param = bit_crush_set_osc_param;
    m->destroy = bit_crush_destroy;
    return m;
}

