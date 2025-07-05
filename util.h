#ifndef UTIL_H
#define UTIL_H

#include <math.h>

#define TWO_PI (2.0f * M_PI)

// Soft wavefolder for simple nonlinearity
float fold(float x, float threshold);

// One-pole smoothing filter
typedef struct {
    float a;
    float b;
    float z;
} CParamSmooth;

void init_smoother(CParamSmooth *s, float a);
float process_smoother(CParamSmooth *s, float in);

#endif
