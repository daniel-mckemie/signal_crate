#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "util.h"
#include "module.h"

static float sine_table[SINE_TABLE_SIZE];
static int sine_table_initialized = 0;

void init_sine_table() {
	if (sine_table_initialized) return;
	for (int i=0; i<SINE_TABLE_SIZE; i++) {
		sine_table[i] = sinf((float)i / SINE_TABLE_SIZE * 2.0f * M_PI);
	}
	sine_table_initialized = 1;
}

const float* get_sine_table(void) {
	return sine_table;
}

// For smoothing VCO, square specifically
float poly_blep(float t, float dt) {
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

void init_smoother(CParamSmooth *s, float a) {
    s->a = a;
    s->b = 1.0f - a;
    s->z = 0.0f;
}

float process_smoother(CParamSmooth *s, float in) {
    s->z = s->b * in + s->a * s->z;
    return s->z;
}

char* trim_whitespace(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

