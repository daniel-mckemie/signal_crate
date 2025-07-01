#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "engine.h"
#include "module.h"

Module* chain[MAX_MODULES];
int module_count = 0;
float sample_rate = 44100.0f;

void process_chain(float* in, float* out, unsigned long frames) {
    float* buffer1 = in;
    float* buffer2 = calloc(frames, sizeof(float));
	int used_in_as_buffer2 = 0;

    for (int i = 0; i < module_count; i++) {
        Module* m = chain[i];

		// Feed raw input if it's the first module and has no input
		float* input_for_this = (i==0) ? in : buffer1;
        m->process(m, input_for_this, buffer2, frames);
		
        float* temp = buffer1;
        buffer1 = buffer2;
        buffer2 = temp;
    }
    if (buffer2 == in)  {
		used_in_as_buffer2 = 1;
	}
	
    memcpy(out, buffer1, frames * sizeof(float));
	if (!used_in_as_buffer2) {
		free(buffer2);
	}

}

void free_module(Module *m) {
    if (!m) return;
    if (m->destroy) {
        m->destroy(m);
    } else {
        if (m->state) free(m->state);  // fallback
    }
    free(m);
}

