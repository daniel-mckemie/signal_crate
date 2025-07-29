#ifndef UTIL_H
#define UTIL_H

#include <math.h>

#define TWO_PI (2.0f * M_PI)
#define SINE_TABLE_SIZE 2048

#ifndef FRAMES_PER_BUFFER
#define FRAMES_PER_BUFFER 512
#endif

float randf(); // random generator

typedef struct {
	float a;
	float b;
	float z;
} CParamSmooth;

// Sine table for VCO, ring mod, etc.
void init_sine_table();
const float* get_sine_table(void);
float poly_blep(float t, float dt);
void init_smoother(CParamSmooth *s, float a);
float process_smoother(CParamSmooth *s, float in);
char* trim_whitespace(char* str);

#endif
