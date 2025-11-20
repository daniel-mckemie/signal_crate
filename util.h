#ifndef UTIL_H
#define UTIL_H

#include <math.h>

#define TWO_PI (2.0f * M_PI)
#define SINE_TABLE_SIZE 2048

#ifndef FRAMES_PER_BUFFER
#define FRAMES_PER_BUFFER 64

#endif

#define BLUE()     attron(COLOR_PAIR(1))
#define GREEN()    attron(COLOR_PAIR(2))
#define YELLOW()   attron(COLOR_PAIR(3))
#define ORANGE()   attron(COLOR_PAIR(4))
#define CLR()      attroff(COLOR_PAIR(1)|COLOR_PAIR(2)|COLOR_PAIR(3)|COLOR_PAIR(4))

#define LABEL(col, txt) do { attron(COLOR_PAIR(col)); printw(txt); CLR(); } while(0)
#define LABEL_AT(col, y, x, txt) do { attron(COLOR_PAIR(col)); mvprintw(y, x, txt); CLR(); } while(0)


extern int truncated;

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
