#ifndef MODULE_LOADER_H
#define MODULE_LOADER_H

#include "module.h"

Module* load_module(const char* name, float sample_rate, const char* args);

#endif
