#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bit_splitter.h"
#include "module.h"
#include "util.h"

// Map normalized float [-1, 1] to uint8 [0, 255]
// Uses full bipolar range: -1.0 -> 0, 0.0 -> 128, 1.0 -> 255
static inline uint8_t float_to_byte(float v) {
    float clamped = fminf(fmaxf(v, -1.0f), 1.0f);
    float scaled = (clamped + 1.0f) * 0.5f * 255.0f;
    return (uint8_t)(int)fminf(fmaxf(scaled, 0.0f), 255.0f);
}

static void bit_splitter_process(Module *m, float *in, unsigned long frames) {
    BitSplitterState *s = (BitSplitterState *)m->state;

    float *input = (m->num_inputs > 0) ? m->inputs[0] : in;
    float *out   = m->output_buffer;

    pthread_mutex_lock(&s->lock);
    float base_thresh = s->thresh;
    int   bit_idx     = s->bit;
    pthread_mutex_unlock(&s->lock);

    float thresh_s = process_smoother(&s->smooth_thresh, base_thresh);

    for (unsigned long i = 0; i < frames; i++) {
        float in_val = input ? input[i] : 0.0f;

        float scaled = in_val / fmaxf(thresh_s, 0.01f);
        clampf(&scaled, -1.0f, 1.0f);

        uint8_t byte_val = float_to_byte(scaled);
        int bit_state = (byte_val >> bit_idx) & 0x01;
        out[i] = bit_state ? 1.0f : 0.0f;
    }
}

static void clamp_params(BitSplitterState *s) {
    clampf(&s->thresh, 0.01f, 2.0f);
    if (s->bit < 0) s->bit = 0;
    if (s->bit > 7) s->bit = 7;
}

static void bit_splitter_draw_ui(Module *m, int y, int x) {
    BitSplitterState *s = (BitSplitterState *)m->state;

    float thresh;
    int bit_idx;

    pthread_mutex_lock(&s->lock);
    thresh  = s->thresh;
    bit_idx = s->bit;
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[BitSplit:%s] ", m->name);
    CLR();

    LABEL(2, "bit:");
    ORANGE();
    printw(" %d | ", bit_idx);
    CLR();

    LABEL(2, "thresh:");
    ORANGE();
    printw(" %.2f", thresh);
    CLR();

    YELLOW();
    mvprintw(y + 1, x, "Keys: -/= (thresh), b/B (bit index)");
    mvprintw(y + 2, x, "Command: :t [thresh], :b [bit 0-7]");
    BLACK();
}

static void bit_splitter_handle_input(Module *m, int key) {
    BitSplitterState *s = (BitSplitterState *)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
        case '=':
            s->thresh += 0.01f;
            handled = 1;
            break;
        case '-':
            s->thresh -= 0.01f;
            handled = 1;
            break;
        case 'b':
            s->bit = (s->bit > 0) ? s->bit - 1 : 0;
            handled = 1;
            break;
        case 'B':
            s->bit = (s->bit < 7) ? s->bit + 1 : 7;
            handled = 1;
            break;
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
                if (type == 't')
                    s->thresh = val;
                else if (type == 'b')
                    s->bit = (int)fminf(fmaxf(val, 0), 7);
            }
            handled = 1;
        } else if (key == 27) {
            s->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) &&
                   s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 &&
                   s->command_index < (int)sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled)
        clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void bit_splitter_set_osc_param(Module *m, const char *param, float value) {
    BitSplitterState *s = (BitSplitterState *)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "thresh") == 0) {
        s->thresh = fminf(fmaxf(value * 2.0f, 0.01f), 2.0f);
    } else if (strcmp(param, "bit") == 0) {
        s->bit = (int)fminf(fmaxf(value * 7.0f, 0.0f), 7.0f);
    } else {
        fprintf(stderr, "[bit_splitter] Unknown OSC param: %s\n", param);
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void bit_splitter_destroy(Module *m) {
    BitSplitterState *s = (BitSplitterState *)m->state;
    if (s)
        pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module *create_module(const char *args, float sample_rate) {
    int   bit    = 0;
    float thresh = 1.0f;

    if (args && strstr(args, "bit="))
        sscanf(strstr(args, "bit="), "bit=%d", &bit);
    if (args && strstr(args, "thresh="))
        sscanf(strstr(args, "thresh="), "thresh=%f", &thresh);

    if (bit < 0) bit = 0;
    if (bit > 7) bit = 7;

    BitSplitterState *s = calloc(1, sizeof(BitSplitterState));
    s->bit         = bit;
    s->thresh      = thresh;
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_thresh, 0.75f);
    clamp_params(s);

    Module *m = calloc(1, sizeof(Module));
    m->name         = "bit_splitter";
    m->state        = s;
    m->process      = bit_splitter_process;
    m->draw_ui      = bit_splitter_draw_ui;
    m->handle_input = bit_splitter_handle_input;
    m->set_param    = bit_splitter_set_osc_param;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->destroy      = bit_splitter_destroy;
    return m;
}
