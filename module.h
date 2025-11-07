#ifndef MODULE_H
#define MODULE_H

#define MAX_MODULES 8192
#define MAX_INPUTS 4096
#define MAX_CONTROL_INPUTS 4096

typedef struct Module {
    const char* name; // Module name for aliases
	const char* type; // Module type
    void (*process)(struct Module*, float* input, unsigned long frames);
	void (*process_control)(struct Module*);
    void (*draw_ui)(struct Module*, int y, int x);
    void (*handle_input)(struct Module*, int key);
	void (*set_param)(struct Module*, const char* param, float value);
	void (*destroy)(struct Module*);
    void* state;
    void* handle;

	// Audio routing
	float* inputs[MAX_INPUTS];
	int num_inputs;
	float* output_bufferL;
	float* output_bufferR;
	float* output_buffer;

	// Control routing
	float* control_inputs[MAX_CONTROL_INPUTS];
	int num_control_inputs;
	float* control_output;
	float control_output_depth;
	const char* control_input_params[MAX_CONTROL_INPUTS];
} Module;

void clampf(float* val, float min, float max);
void destroy_base_module(struct Module* m);


#endif

