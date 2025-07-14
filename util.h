#ifndef UTIL_H
#define UTIL_H

#include <math.h>

#define TWO_PI (2.0f * M_PI)
#define SINE_TABLE_SIZE 2048

#ifndef FRAMES_PER_BUFFER
#define FRAMES_PER_BUFFER 512
#endif

// Sine table for VCO, ring mod, etc.
static float sine_table[SINE_TABLE_SIZE];
static int sine_table_initialized = 0;

static inline void init_sine_table() {
	if (sine_table_initialized) return;
	for (int i=0; i<SINE_TABLE_SIZE; i++) {
		sine_table[i] = sinf((float)i / SINE_TABLE_SIZE * 2.0f * M_PI);
	}
	sine_table_initialized = 1;
}

// For smoothing VCO, square specifically
static inline float poly_blep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    } else {
        return 0.0f;
    }
}

// One-pole smoothing filter
typedef struct {
    float a;
    float b;
    float z;
} CParamSmooth;

void init_smoother(CParamSmooth *s, float a);
float process_smoother(CParamSmooth *s, float in);

char* trim_whitespace(char* str);

#endif
