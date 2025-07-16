#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "util.h"
#include "module.h"

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

