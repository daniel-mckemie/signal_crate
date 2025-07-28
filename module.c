#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "module.h"

void destroy_base_module(Module* m) {
    if (!m) return;

    if (m->state) free(m->state);
    if (m->name) free((void*)m->name);
    if (m->output_buffer) free(m->output_buffer);
    free(m);
}

void clampf(float* val, float min, float max) {
	if (*val < min) *val = min;
	if (*val > max) *val = max;
}
