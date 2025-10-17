#ifndef SCRIPTBOX_H
#define SCRIPTBOX_H

#include <pthread.h>

typedef struct ScriptBox {
    pthread_mutex_t lock;

    char script_text[2048];   // multi-line buffer
    int cursor_pos;
    int editing;              // 1 = editing mode
    int scroll_offset;

    float sample_rate;

    // --- command support ---
    char command_buffer[256];
    int command_index;
    int entering_command;

    // --- status ---
    char last_result[128];

    // --- scheduling state ---
    int looping;
    unsigned int interval_ms;
    char loop_cmd[256];
    pthread_t loop_thread;
} ScriptBox;

typedef struct {
    char script_line[256];
    void* scriptbox_ptr;  // pointer back to ScriptBox
} ScriptEvent;

void run_script_line_cb(void* userdata);

#endif

