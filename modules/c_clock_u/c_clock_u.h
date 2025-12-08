#ifndef C_CLOCK_U_H
#define C_CLOCK_U_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    float bpm;
    float pw;     // pulse width (0–1)
    float mult;   // mult/div factor

    float last_gate;
    double phase;
    float sample_rate;

    int running;

    // UI display mirrors
    float display_bpm;
    float display_pw;
    float display_mult;
    int   display_running;

    // Command mode
    bool  entering_command;
    char  command_buffer[64];
    int   command_index;

    pthread_mutex_t lock;
} CClockU;

#endif

