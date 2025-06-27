#ifndef MODULE_H
#define MODULE_H

typedef struct Module {
    const char* name;

    void (*process)(struct Module* self, float* input, float* output, unsigned long frames);
    void (*set_param)(struct Module* self, const char* param, float value);
    void (*connect_input)(struct Module* self, int input_index, float* source);
    float* output;
    void* state;
} Module;

#endif

