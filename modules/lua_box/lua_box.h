#ifndef LUA_BOX_H
#define LUA_BOX_H

#include <pthread.h>

typedef struct LuaBox {
    pthread_mutex_t lock;

    /* editor buffer + state */
    char script_text[4096];
    int cursor_pos;
    int editing;

    float sample_rate;

    /* status line shown under the editor */
    char last_result[128];

    /* background loop thread (one per box) */
    pthread_t loop_thread;
    int loop_running;     /* 1 while a script loop is active */
    int loop_should_exit; /* set by Ctrl-R / ESC / destroy to signal stop */

    /* owned Lua state (opaque here to avoid pulling lua.h into every include)
     */
    void *L;
} LuaBox;

#endif
