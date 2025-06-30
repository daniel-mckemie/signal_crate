#ifndef MODULE_H
#define MODULE_H

#define MAX_MODULES 16

typedef struct Module {
    const char* name;
    void (*process)(struct Module*, float* in, float* out, unsigned long frames);
    void (*draw_ui)(struct Module*, int row); // Optional
    void (*handle_input)(struct Module*, int key); // 👈 Add this line
    void* state;
    void* handle;
} Module;

#endif

