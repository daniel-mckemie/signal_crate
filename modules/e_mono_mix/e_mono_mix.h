#ifndef E_MONO_MIX_H
#define E_MONO_MIX_H

#include "module.h"

typedef struct {
    int executed;
} EMonoMix;

Module* create_module(const char* args, float sample_rate);

#endif

