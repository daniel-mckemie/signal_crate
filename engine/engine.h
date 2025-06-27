#ifndef ENGINE_H
#define ENGINE_H

#include "module.h"

#define MAX_MODULES 16

extern Module* chain[MAX_MODULES];
extern int module_count;
extern float sample_rate;

void process_chain(float* in, float* out, unsigned long frames);

#endif
