#ifndef E_POLYWAV_SPLIT_H
#define E_POLYWAV_SPLIT_H

#include "module.h"

typedef struct {
    int executed;
} EPolywavSplit;

Module* create_module(const char* args, float sample_rate);

#endif

