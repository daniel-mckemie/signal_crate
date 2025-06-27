#ifndef PATCH_H
#define PATCH_H

#include "module.h"

#define MAX_MODULES 32
#define BUFFER_SIZE 512

typedef struct Patch {
    Module* modules[MAX_MODULES];
    int module_count;
    float sample_rate;
    unsigned long frame_count;
} Patch;

#endif

