#ifndef UTIL_H
#define UTIL_H

#include <math.h>

#define TWO_PI (2.0f * M_PI)

// This is a function that handles smoothing of params with real time changes
typedef struct {
	float a;
	float b;
	float z;
} CParamSmooth;

void init_smoother(CParamSmooth *s, float a);
float process_smoother(CParamSmooth *s, float in);

#endif
