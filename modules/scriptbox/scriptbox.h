#ifndef SCRIPTBOX_H
#define SCRIPTBOX_H

#include <pthread.h>

typedef struct {
    pthread_mutex_t lock;
	float sample_rate;
	
    char command_buffer[256];
    int command_index;
    int entering_command;
    char last_result[128];
} ScriptBox;

#endif

