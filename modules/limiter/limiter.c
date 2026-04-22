#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "limiter.h"
#include "module.h"
#include "util.h"

#define MAX_LOOKAHEAD_MS 10.0f
#define MIN_RELEASE_MS 1.0f
#define MAX_RELEASE_MS 1000.0f

static void limiter_process(Module *m, float *in, unsigned long frames) {
    LimiterState *state = (LimiterState *)m->state;
    float *input = (m->num_inputs > 0) ? m->inputs[0] : in;
    float *out = m->output_buffer;

    if (!input) {
        memset(out, 0, frames * sizeof(float));
        return;
    }

    pthread_mutex_lock(&state->lock);
    float base_threshold = state->threshold;
    float base_release = state->release;
    float sample_rate = state->sample_rate;
    pthread_mutex_unlock(&state->lock);

    float threshold_s =
        process_smoother(&state->smooth_threshold, base_threshold);
    float release_s = process_smoother(&state->smooth_release, base_release);

    float disp_threshold = threshold_s;
    float disp_release = release_s;
    float max_reduction = 0.0f;

    // Convert release time to coefficient
    float release_coeff = expf(-1.0f / (release_s * 0.001f * sample_rate));

    for (unsigned long i = 0; i < frames; i++) {
        float threshold = threshold_s;
        float release = release_s;

        // Handle CV inputs
        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j])
                continue;

            const char *param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "threshold") == 0) {
                threshold += control * 0.5f; // ±0.5 range
            } else if (strcmp(param, "release") == 0) {
                release += control * 100.0f; // ±100ms range
            }
        }

        // Clamp parameters
        clampf(&threshold, 0.1f, 1.0f);
        clampf(&release, MIN_RELEASE_MS, MAX_RELEASE_MS);

        disp_threshold = threshold;
        disp_release = release;

        float in_sample = input[i];

        // Store input in delay buffer for lookahead
        state->delay_buffer[state->delay_index] = in_sample;

        // Get delayed sample for output
        int output_index =
            (state->delay_index - state->delay_samples + 1024) % 1024;
        float delayed_sample = state->delay_buffer[output_index];

        // Peak detection with lookahead
        float peak = 0.0f;
        for (int k = 0; k < state->delay_samples; k++) {
            int idx = (state->delay_index - k + 1024) % 1024;
            float sample_abs = fabsf(state->delay_buffer[idx]);
            if (sample_abs > peak) {
                peak = sample_abs;
            }
        }

        // Calculate required gain reduction
        float target_gain = 1.0f;
        if (peak > threshold) {
            target_gain = threshold / peak;
        }

        // Smooth gain reduction with release
        if (target_gain < state->envelope) {
            // Attack: instant
            state->envelope = target_gain;
        } else {
            // Release: smooth
            state->envelope =
                target_gain + (state->envelope - target_gain) * release_coeff;
        }

        // Apply limiting
        float limited_sample = delayed_sample * state->envelope;

        // Track maximum reduction for display
        float reduction_db = 20.0f * log10f(fmaxf(state->envelope, 0.001f));
        if (reduction_db < max_reduction) {
            max_reduction = reduction_db;
        }

        out[i] = limited_sample;

        // Advance delay buffer index
        state->delay_index = (state->delay_index + 1) % 1024;
    }

    // Update display values
    pthread_mutex_lock(&state->lock);
    state->display_threshold = disp_threshold;
    state->display_release = disp_release;
    state->display_reduction = max_reduction;
    pthread_mutex_unlock(&state->lock);
}

static void clamp_params(LimiterState *state) {
    clampf(&state->threshold, 0.1f, 1.0f);
    clampf(&state->release, MIN_RELEASE_MS, MAX_RELEASE_MS);
    clampf(&state->lookahead_ms, 0.1f, MAX_LOOKAHEAD_MS);
}

static void limiter_draw_ui(Module *m, int y, int x) {
    LimiterState *state = (LimiterState *)m->state;

    float threshold, release, reduction;

    pthread_mutex_lock(&state->lock);
    threshold = state->display_threshold;
    release = state->display_release;
    reduction = state->display_reduction;
    pthread_mutex_unlock(&state->lock);

    BLUE();
    mvprintw(y, x, "[Limiter:%s] ", m->name);
    CLR();

    LABEL(2, "thresh:");
    ORANGE();
    printw(" %.2f | ", threshold);
    CLR();

    LABEL(2, "rel:");
    ORANGE();
    printw(" %.1f ms | ", release);
    CLR();

    LABEL(2, "reduction:");
    if (reduction < -0.1f) {
        ORANGE();
        printw(" %.1f dB", reduction);
        CLR();
    } else {
        GREEN();
        printw(" %.1f dB", reduction);
        CLR();
    }

    YELLOW();
    mvprintw(y + 1, x, "Real-time keys: -/= (thresh), [/] (rel)");
    mvprintw(y + 2, x, "Command mode: :1 [thresh], :2 [rel]");
    BLACK();
}

static void limiter_handle_input(Module *m, int key) {
    LimiterState *state = (LimiterState *)m->state;
    int handled = 0;

    pthread_mutex_lock(&state->lock);

    if (!state->entering_command) {
        switch (key) {
        case '=':
            state->threshold += 0.05f;
            handled = 1;
            break;
        case '-':
            state->threshold -= 0.05f;
            handled = 1;
            break;
        case ']':
            state->release += 5.0f;
            handled = 1;
            break;
        case '[':
            state->release -= 5.0f;
            handled = 1;
            break;
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
            if (sscanf(state->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1')
                    state->threshold = val;
                else if (type == '2')
                    state->release = val;
            }
            handled = 1;
        } else if (key == 27) {
            state->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) &&
                   state->command_index > 0) {
            state->command_index--;
            state->command_buffer[state->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 &&
                   state->command_index < sizeof(state->command_buffer) - 1) {
            state->command_buffer[state->command_index++] = (char)key;
            state->command_buffer[state->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled)
        clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void limiter_set_osc_param(Module *m, const char *param, float value) {
    LimiterState *state = (LimiterState *)m->state;
    pthread_mutex_lock(&state->lock);

    if (strcmp(param, "thresh") == 0) {
        state->threshold = fminf(fmaxf(value, 0.1f), 1.0f);
    } else if (strcmp(param, "rel") == 0) {
        float norm = fminf(fmaxf(value, 0.0f), 1.0f);
        state->release =
            MIN_RELEASE_MS + norm * (MAX_RELEASE_MS - MIN_RELEASE_MS);
    } else {
        fprintf(stderr, "[Limiter] Unknown OSC param: %s\n", param);
    }
    clamp_params(state);
    pthread_mutex_unlock(&state->lock);
}

static void limiter_destroy(Module *m) {
    LimiterState *state = (LimiterState *)m->state;
    if (state) {
        if (state->delay_buffer) {
            free(state->delay_buffer);
        }
        pthread_mutex_destroy(&state->lock);
    }
    destroy_base_module(m);
}

Module *create_module(const char *args, float sample_rate) {
    float threshold = 0.95f;   // Default threshold just below clipping
    float release = 50.0f;     // Default 50ms release
    float lookahead_ms = 5.0f; // Default 5ms lookahead

    // Parse arguments
    if (args && strstr(args, "thresh=")) {
        sscanf(strstr(args, "thresh="), "thresh=%f", &threshold);
    }

    if (args && strstr(args, "rel=")) {
        sscanf(strstr(args, "rel="), "rel=%f", &release);
    }

    if (args && strstr(args, "look=")) {
        sscanf(strstr(args, "look="), "look=%f", &lookahead_ms);
    }

    LimiterState *s = calloc(1, sizeof(LimiterState));
    s->threshold = threshold;
    s->release = release;
    s->lookahead_ms = lookahead_ms;
    s->sample_rate = sample_rate;
    s->envelope = 1.0f;

    // Calculate delay buffer size for lookahead
    s->delay_samples = (int)(lookahead_ms * 0.001f * sample_rate);
    if (s->delay_samples > 1024)
        s->delay_samples = 1024;
    if (s->delay_samples < 1)
        s->delay_samples = 1;

    // Allocate delay buffer
    s->delay_buffer = calloc(1024, sizeof(float));
    s->delay_index = 0;

    // Initialize mutex and smoothers
    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_threshold, 0.9f);
    init_smoother(&s->smooth_release, 0.9f);
    clamp_params(s);

    Module *m = calloc(1, sizeof(Module));
    m->name = "limiter";
    m->state = s;

    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = limiter_process;
    m->draw_ui = limiter_draw_ui;
    m->handle_input = limiter_handle_input;
    m->set_param = limiter_set_osc_param;
    m->destroy = limiter_destroy;

    return m;
}
