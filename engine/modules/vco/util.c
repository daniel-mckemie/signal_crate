#include "util.h"

// This is the smoother function in the header
// can be any util functions
void init_smoother(CParamSmooth *s, float a) {
	s->a = a;
	s->b = 1.0f - a;
	s->z = 0.0f;
}

float process_smoother(CParamSmooth *s, float in) {
	s->z = s->b * in + s->a * s->z;
	return s->z;
}
