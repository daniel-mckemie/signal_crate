#ifndef MODULE_H
#define MODULE_H

typedef struct Module {
    const char* name;
    void (*process)(struct Module*, float* in, float* out, unsigned long frames);
    void (*draw_ui)(struct Module*, int row); // Optional
    void* state;
    void* handle;
} Module;

#endif

