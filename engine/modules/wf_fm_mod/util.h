#ifndef UTIL_H
#define UTIL_H

#include <math.h>

float fold(float x, float threshold);

typedef struct {
	float a;
	float b;
	float z;
} CParamSmooth;

void init_smoother(CParamSmooth *s, float a);
float process_smoother(CParamSmooth *s, float in);

#endif
