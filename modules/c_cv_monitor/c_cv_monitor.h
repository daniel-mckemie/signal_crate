#ifndef C_CV_MONITOR
#define C_CV_MONITOR

#include <pthread.h>
#include "util.h"
#include "module.h"

typedef struct {
    float attenuvert;
    float offset;
    float input;
    float output;
    float sample_rate;

    CParamSmooth smooth_att;
    CParamSmooth smooth_off;

    pthread_mutex_t lock;
	
	float display_input;
	float display_output;
	float display_att;
	float display_off;

    char command_buffer[32];
    int command_index;
    bool entering_command;
} CCVMonitor;

#endif
