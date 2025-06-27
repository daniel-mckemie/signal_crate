#include <stdlib.h>
#include <string.h>

#include "engine.h"


Module* chain[MAX_MODULES];
int module_count = 0;
float sample_rate = 44100.0f;

void process_chain(float* in, float* out, unsigned long frames) {
    float* buffer1 = in;
    float* buffer2 = calloc(frames, sizeof(float));

    for (int i = 0; i < module_count; i++) {
        Module* m = chain[i];
        m->process(m, buffer1, buffer2, frames);
        float* temp = buffer1;
        buffer1 = buffer2;
        buffer2 = temp;
    }
    memcpy(out, buffer1, frames * sizeof(float));
    free(buffer2);
}
