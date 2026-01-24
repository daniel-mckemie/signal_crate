#ifndef E_NORMALIZE_H
#define E_NORMALIZE_H

#include "module.h"

typedef struct {
    int executed;
} ENormalize;

Module* create_module(const char* args, float sample_rate);

#endif

