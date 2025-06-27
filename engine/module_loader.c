#include <stdio.h>
#include <dlfcn.h>
#include "module_loader.h"

Module* load_module(const char* path, float sample_rate) {
    void* handle = dlopen(path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "Failed to load %s\n", path);
        return NULL;
    }

    Module* (*create)(float) = dlsym(handle, "create_module");
    if (!create) {
        fprintf(stderr, "Missing create_module in %s\n", path);
        return NULL;
    }

    return create(sample_rate);
}

