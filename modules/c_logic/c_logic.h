#ifndef C_LOGIC_H
#define C_LOGIC_H

#include <pthread.h>
#include "util.h"

typedef enum {
	AND,
	OR,
	XOR,
	NAND,
	NOR,
	XNOR,
	NOT
} LogicType;

typedef struct {
	LogicType logic_type;
	
	// UI display params, any that you want to have in UI
	float display_in1;
	float display_in2;

	pthread_mutex_t lock;

	// For command mode, keyboard control
	bool entering_command;
	char command_buffer[64];
	int command_index;
} CLogic; // Name of module

#endif
