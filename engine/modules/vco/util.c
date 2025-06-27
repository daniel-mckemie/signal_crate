#include "util.h"

void init_smoother(CParamSmooth *s, float a) {
    s->a = a;
    s->b = 1.0f - a;
    s->z = 0.0f;
}

float process_smoother(CParamSmooth *s, float in) {
    s->z = s->b * in + s->a * s->z;
    return s->z;
}
