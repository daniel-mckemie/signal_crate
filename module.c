#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "module.h"

void destroy_base_module(Module* m) {
    if (!m) return;

    for (int i = 0; i < m->num_control_inputs; i++) {
        if (m->control_input_params[i]) {
            free((void*)m->control_input_params[i]); /* strdup */
            m->control_input_params[i] = NULL;
        }
    }

    free(m->output_buffer);
    free(m->output_bufferL);
    free(m->output_bufferR);
    free(m->control_output);
    free(m->state);
    free((void*)m->name);

    free(m);
}

void clampf(float* val, float min, float max) {
	if (*val < min) *val = min;
	if (*val > max) *val = max;
}
