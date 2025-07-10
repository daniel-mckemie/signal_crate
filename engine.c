#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "engine.h"
#include "util.h"
#include "module.h"

Module* chain[MAX_MODULES];
int module_count = 0;
float sample_rate = 44100.0f;

void process_chain(float* in, float* out, unsigned long frames) {
    for (int i = 0; i < module_count; i++) {
        Module* m = chain[i];

        // Clear output buffer
        if (m->output_buffer)
            memset(m->output_buffer, 0, sizeof(float) * frames);

        float mixed_input[FRAMES_PER_BUFFER] = {0};

        if (m->num_inputs == 0 && i == 0 && in != NULL) {
            // Use local audio input for first module if unconnected
            memcpy(mixed_input, in, sizeof(float) * frames);
        } else if (m->num_inputs > 0) {
            // Mix all connected inputs
            for (int j = 0; j < m->num_inputs; j++) {
                float* input_buf = m->inputs[j];
                if (!input_buf) continue;
                for (unsigned long k = 0; k < frames; k++) {
                    mixed_input[k] += input_buf[k];
                }
            }

            // Normalize to avoid clipping
            float scale = 1.0f / m->num_inputs;
            for (unsigned long k = 0; k < frames; k++) {
                mixed_input[k] *= scale;
            }
        } else {
            // No inputs and not first module — process silence
            memset(mixed_input, 0, sizeof(float) * frames);
        }

        m->process(m, mixed_input, frames);
    }

    // Final output is from the last module
    if (module_count > 0) {
        float* buffer = chain[module_count - 1]->output_buffer;
        memcpy(out, buffer, frames * sizeof(float));
    } else {
        memset(out, 0, frames * sizeof(float));
    }
}

void connect(Module* src, Module* dst) {
    if (dst->num_inputs < MAX_INPUTS)
        dst->inputs[dst->num_inputs++] = src->output_buffer;
}

void free_module(Module *m) {
    if (!m) return;
    if (m->destroy) {
        m->destroy(m);
    } else if (m->state) {
		free(m->state);
    }
	if (m->output_buffer) free(m->output_buffer);
    free(m);
}

