#ifndef MODULE_H
#define MODULE_H

#define MAX_MODULES 64
#define MAX_INPUTS 128

typedef struct Module {
    const char* name;
    void (*process)(struct Module*, float* input, unsigned long frames);
    void (*draw_ui)(struct Module*, int y, int x);
    void (*handle_input)(struct Module*, int key);
	void (*destroy)(struct Module*);
    void* state;
    void* handle;

	// Audio routing
	float* inputs[MAX_INPUTS];
	int num_inputs;

	float* output_buffer;
} Module;

#endif

