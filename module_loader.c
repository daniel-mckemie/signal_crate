#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "module_loader.h"

Module* load_module(const char* name, float sample_rate, const char* args) {
    char path[256];
    snprintf(path, sizeof(path), "./modules/%s/%s.dylib", name, name);

    void* handle = dlopen(path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "Failed to load module %s: %s\n", name, dlerror());
        return NULL;
    }

    Module* (*create)(const char*, float) = dlsym(handle, "create_module");
    if (!create) {
        fprintf(stderr, "No create_module in %s\n", name);
        return NULL;
    }

    Module* m = create(args, sample_rate);
    m->handle = handle;
    return m;
}
