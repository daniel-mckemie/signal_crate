#ifndef ENGINE_H
#define ENGINE_H

#include "module.h"
#include "scheduler.h"

typedef struct {
    char name[32];
    Module* module;
} NamedModule;

// DAG patching API
void initialize_engine(const char* patch_text);
void shutdown_engine(void);
void process_audio(float* input, float* output, unsigned long frames);

Module* get_module(int index);
int get_module_count(void);
const char* get_module_alias(int index);

#endif
