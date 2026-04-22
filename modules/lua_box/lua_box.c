/*
 * lua_box — live Lua scripting for Signal Crate.
 *
 * UI / lifecycle match scriptbox (Enter = edit, ESC = exit edit, Ctrl-R = run).
 * Scripts send CV to aliased params via OSC — same transport as scriptbox,
 * so no engine-side changes are needed.
 *
 * Lua API available inside a script:
 *
 *   set(alias, param, v)              -- raw 0..1 write to /alias/param
 *   setn(alias, param, v, lo, hi)     -- linear-normalize v in [lo,hi] -> 0..1
 *   setf(alias, param, hz)            -- log-normalize 20..20000 -> 0..1
 *   rand(a, b)                        -- uniform random in [a,b]
 *   sleep(ms)                         -- sleep (only meaningful inside loop
 * body) log(str)                          -- write to the box's status line
 *   loop(ms, function() ... end)      -- run body repeatedly every ms until
 * stopped
 *
 * Running a new script (Ctrl-R) cancels any previous loop on this box.
 * Pressing Ctrl-R on an empty script also stops the current loop.
 */

#include <lauxlib.h>
#include <lo/lo.h>
#include <lua.h>
#include <lualib.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lua_box.h"
#include "module.h"
#include "util.h"

/* ------------------------------------------------------------------ */
/* OSC send — identical transport to scriptbox                         */
/* ------------------------------------------------------------------ */

static void send_osc(const char *alias, const char *param, float value) {
    const char *port = getenv("SIGNAL_CRATE_OSC_PORT");
    if (!port || !*port)
        return;
    lo_address t = lo_address_new("127.0.0.1", port);
    if (!t)
        return;

    char path[128];
    snprintf(path, sizeof(path), "/%s/%s", alias, param);
    lo_send(t, path, "f", value);
    lo_address_free(t);
}

/* ------------------------------------------------------------------ */
/* Lua bindings                                                        */
/* ------------------------------------------------------------------ */

/* Retrieve the LuaBox* we stashed in the registry at state creation. */
static LuaBox *lb_from_state(lua_State *L) {
    lua_pushstring(L, "__luabox_self");
    lua_gettable(L, LUA_REGISTRYINDEX);
    LuaBox *s = (LuaBox *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return s;
}

/* set(alias, param, value) — writes 0..1 value directly */
static int l_set(lua_State *L) {
    const char *alias = luaL_checkstring(L, 1);
    const char *param = luaL_checkstring(L, 2);
    float v = (float)luaL_checknumber(L, 3);
    if (v < 0.f)
        v = 0.f;
    if (v > 1.f)
        v = 1.f;
    send_osc(alias, param, v);
    return 0;
}

/* setn(alias, param, v, lo, hi) — linear normalize v in [lo,hi] to [0,1] */
static int l_setn(lua_State *L) {
    const char *alias = luaL_checkstring(L, 1);
    const char *param = luaL_checkstring(L, 2);
    float v = (float)luaL_checknumber(L, 3);
    float lo = (float)luaL_checknumber(L, 4);
    float hi = (float)luaL_checknumber(L, 5);
    float scaled = (hi > lo) ? (v - lo) / (hi - lo) : v;
    if (scaled < 0.f)
        scaled = 0.f;
    if (scaled > 1.f)
        scaled = 1.f;
    send_osc(alias, param, scaled);
    return 0;
}

/* setf(alias, param, hz) — log normalize 20..20000 to 0..1 */
static int l_setf(lua_State *L) {
    const char *alias = luaL_checkstring(L, 1);
    const char *param = luaL_checkstring(L, 2);
    float hz = (float)luaL_checknumber(L, 3);
    const float fmin = 20.0f, fmax = 20000.0f;
    if (hz < fmin)
        hz = fmin;
    if (hz > fmax)
        hz = fmax;
    float scaled = logf(hz / fmin) / logf(fmax / fmin);
    send_osc(alias, param, scaled);
    return 0;
}

/* rand(a, b) — uniform random in [a,b] */
static int l_rand(lua_State *L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    double r = a + randf() * (b - a);
    lua_pushnumber(L, r);
    return 1;
}

/* sleep(ms) — cancellable sleep; inside a loop body this is a cancellation
 * point, so Ctrl-R can interrupt promptly. */
static int l_sleep(lua_State *L) {
    double ms = luaL_checknumber(L, 1);
    if (ms < 0)
        ms = 0;
    LuaBox *s = lb_from_state(L);

    /* poll every 10ms so loop_should_exit is honored within a reasonable time
     * even for long sleeps, without busy-waiting. */
    double remaining = ms;
    while (remaining > 0.0 && !(s && s->loop_should_exit)) {
        double chunk = remaining > 10.0 ? 10.0 : remaining;
        struct timespec ts;
        ts.tv_sec = (time_t)(chunk / 1000.0);
        ts.tv_nsec = (long)((chunk - ts.tv_sec * 1000.0) * 1e6);
        nanosleep(&ts, NULL);
        remaining -= chunk;
    }
    return 0;
}

/* log(str) — write to the status line of this box */
static int l_log(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    LuaBox *s = lb_from_state(L);
    if (s) {
        pthread_mutex_lock(&s->lock);
        snprintf(s->last_result, sizeof(s->last_result), "%.127s", msg);
        pthread_mutex_unlock(&s->lock);
    }
    return 0;
}

/* Storage for the deferred loop body: we stash the ms interval + a ref to the
 * Lua function in the registry, then the worker thread resumes that closure. */
typedef struct {
    LuaBox *box;
    double interval_ms;
    int fn_ref; /* LUA_REGISTRYINDEX ref to the closure */
} LoopCtx;

static void *loop_thread_fn(void *arg) {
    LoopCtx *ctx = (LoopCtx *)arg;
    LuaBox *s = ctx->box;
    lua_State *L = (lua_State *)s->L;

    while (!s->loop_should_exit) {
        /* push the stored function and call it with no args */
        lua_rawgeti(L, LUA_REGISTRYINDEX, ctx->fn_ref);
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char *err = lua_tostring(L, -1);
            pthread_mutex_lock(&s->lock);
            snprintf(s->last_result, sizeof(s->last_result),
                     "lua error: %.100s", err ? err : "?");
            pthread_mutex_unlock(&s->lock);
            lua_pop(L, 1);
            break; /* don't hammer on errors */
        }

        /* sleep for interval_ms, checking for exit */
        double remaining = ctx->interval_ms;
        while (remaining > 0.0 && !s->loop_should_exit) {
            double chunk = remaining > 10.0 ? 10.0 : remaining;
            struct timespec ts;
            ts.tv_sec = (time_t)(chunk / 1000.0);
            ts.tv_nsec = (long)((chunk - ts.tv_sec * 1000.0) * 1e6);
            nanosleep(&ts, NULL);
            remaining -= chunk;
        }
    }

    /* release the closure ref */
    luaL_unref(L, LUA_REGISTRYINDEX, ctx->fn_ref);
    free(ctx);
    s->loop_running = 0;
    return NULL;
}

/* loop(ms, fn) — store fn + spawn worker.  Only one loop per box; calling
 * loop() again from within a running script replaces the previous. */
static int l_loop(lua_State *L) {
    double ms = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    LuaBox *s = lb_from_state(L);
    if (!s)
        return 0;

    if (ms < 1.0)
        ms = 1.0; /* guard against a tight CPU burn */

    /* tear down any existing loop before starting a new one */
    if (s->loop_running) {
        s->loop_should_exit = 1;
        pthread_join(s->loop_thread, NULL);
        s->loop_running = 0;
    }

    /* stash the function in the registry and start the worker */
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    LoopCtx *ctx = (LoopCtx *)calloc(1, sizeof(LoopCtx));
    ctx->box = s;
    ctx->interval_ms = ms;
    ctx->fn_ref = ref;

    s->loop_should_exit = 0;
    s->loop_running = 1;
    if (pthread_create(&s->loop_thread, NULL, loop_thread_fn, ctx) != 0) {
        s->loop_running = 0;
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        free(ctx);
        luaL_error(L, "failed to start loop thread");
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lua state construction                                              */
/* ------------------------------------------------------------------ */

static void lua_state_open(LuaBox *s) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L); /* math, string, table — handy for live patching */

    /* stash self pointer in registry so bindings can recover it */
    lua_pushstring(L, "__luabox_self");
    lua_pushlightuserdata(L, s);
    lua_settable(L, LUA_REGISTRYINDEX);

    /* register our API as globals */
    lua_pushcfunction(L, l_set);
    lua_setglobal(L, "set");
    lua_pushcfunction(L, l_setn);
    lua_setglobal(L, "setn");
    lua_pushcfunction(L, l_setf);
    lua_setglobal(L, "setf");
    lua_pushcfunction(L, l_rand);
    lua_setglobal(L, "rand");
    lua_pushcfunction(L, l_sleep);
    lua_setglobal(L, "sleep");
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "log");
    lua_pushcfunction(L, l_loop);
    lua_setglobal(L, "loop");

    s->L = L;
}

static void lua_state_close(LuaBox *s) {
    if (s->L) {
        lua_close((lua_State *)s->L);
        s->L = NULL;
    }
}

/* Stop any running loop thread and wait for it. */
static void stop_loop(LuaBox *s) {
    if (s->loop_running) {
        s->loop_should_exit = 1;
        pthread_join(s->loop_thread, NULL);
        s->loop_running = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Script execution                                                    */
/* ------------------------------------------------------------------ */

static void run_script(LuaBox *s) {
    /* snapshot the script text under the lock */
    char copy[sizeof(s->script_text)];
    pthread_mutex_lock(&s->lock);
    strncpy(copy, s->script_text, sizeof(copy));
    copy[sizeof(copy) - 1] = '\0';
    pthread_mutex_unlock(&s->lock);

    /* stop prior loop and rebuild the state so old globals/state don't leak */
    stop_loop(s);
    lua_state_close(s);
    lua_state_open(s);

    /* empty script = just stop everything */
    int empty = 1;
    for (const char *p = copy; *p; p++) {
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            empty = 0;
            break;
        }
    }
    if (empty) {
        pthread_mutex_lock(&s->lock);
        snprintf(s->last_result, sizeof(s->last_result), "stopped");
        pthread_mutex_unlock(&s->lock);
        return;
    }

    lua_State *L = (lua_State *)s->L;
    if (luaL_loadstring(L, copy) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        pthread_mutex_lock(&s->lock);
        snprintf(s->last_result, sizeof(s->last_result), "parse: %.110s",
                 err ? err : "?");
        pthread_mutex_unlock(&s->lock);
        lua_pop(L, 1);
        return;
    }

    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        pthread_mutex_lock(&s->lock);
        snprintf(s->last_result, sizeof(s->last_result), "run: %.115s",
                 err ? err : "?");
        pthread_mutex_unlock(&s->lock);
        lua_pop(L, 1);
        return;
    }

    pthread_mutex_lock(&s->lock);
    /* if a loop is running, say so; otherwise the script just finished */
    snprintf(s->last_result, sizeof(s->last_result),
             s->loop_running ? "running" : "ok");
    pthread_mutex_unlock(&s->lock);
}

/* ------------------------------------------------------------------ */
/* Module callbacks (UI + lifecycle)                                   */
/* ------------------------------------------------------------------ */

static void lua_box_process_control(Module *m, unsigned long frames) {
    (void)m;
    (void)frames;
    /* no continuous output — Lua thread does all the work */
}

static void lua_box_draw_ui(Module *m, int y, int x) {
    LuaBox *s = (LuaBox *)m->state;
    pthread_mutex_lock(&s->lock);

    mvprintw(y, x, "[Lua:%s] (Enter edit | ESC exit | Ctrl-R run/stop) %s",
             m->name, s->loop_running ? "*" : " ");

    /* show first 6 lines of the script in the summary view */
    int max_lines = 6;
    int idx = 0;
    for (int l = 0; l < max_lines && s->script_text[idx]; l++) {
        char linebuf[128];
        int len = 0;
        while (s->script_text[idx] && s->script_text[idx] != '\n' &&
               len < 127) {
            linebuf[len++] = s->script_text[idx++];
        }
        linebuf[len] = '\0';
        if (s->script_text[idx] == '\n')
            idx++;
        mvprintw(y + 1 + l, x, "%-70s", linebuf);
    }

    mvprintw(y + 8, x, "Result: %.60s", s->last_result);

    pthread_mutex_unlock(&s->lock);
}

static void lua_box_handle_input(Module *m, int key) {
    LuaBox *s = (LuaBox *)m->state;

    /* ---- Enter = drop into fullscreen edit mode (same UX as scriptbox) ----
     */
    if (!s->editing && key == '\n') {
        s->editing = 1;
        keypad(stdscr, TRUE);
        nodelay(stdscr, FALSE);
        curs_set(1);

        int ch;
        while (s->editing) {
            pthread_mutex_lock(&s->lock);

            erase();
            mvprintw(0, 0, "[LuaBox] (Ctrl-R run/stop | ESC exit)  %s",
                     s->loop_running ? "[running]" : "");

            int cur_line = 0, cur_col = 0;
            for (int i = 0; i < s->cursor_pos; i++) {
                if (s->script_text[i] == '\n') {
                    cur_line++;
                    cur_col = 0;
                } else
                    cur_col++;
            }

            int max_lines = 20;
            int idx = 0;
            for (int l = 0; l < max_lines && s->script_text[idx]; l++) {
                char linebuf[128];
                int len = 0;
                while (s->script_text[idx] && s->script_text[idx] != '\n' &&
                       len < 127)
                    linebuf[len++] = s->script_text[idx++];
                linebuf[len] = '\0';
                if (s->script_text[idx] == '\n')
                    idx++;
                mvprintw(2 + l, 2, "%-76s", linebuf);
            }

            /* status line at the bottom of the screen */
            {
                int rows, cols;
                (void)cols;
                getmaxyx(stdscr, rows, cols);
                mvprintw(rows - 1, 2, "Result: %-70.70s", s->last_result);
            }

            move(2 + cur_line, 2 + cur_col);
            refresh();
            pthread_mutex_unlock(&s->lock);

            ch = getch();

            /* Ctrl-R inside the editor: run without leaving edit mode.
             * run_script() takes the lock itself, so call it outside the
             * editor's critical section. */
            if (ch == 18) {
                curs_set(0);
                run_script(s);
                pthread_mutex_lock(&s->lock);
                /* redraw the status line immediately so feedback is visible */
                int rows, cols;
                (void)cols;
                getmaxyx(stdscr, rows, cols);
                mvprintw(rows - 1, 2, "Result: %-70.70s", s->last_result);
                pthread_mutex_unlock(&s->lock);
                curs_set(1);
                continue;
            }

            pthread_mutex_lock(&s->lock);
            switch (ch) {
            case 27:
                s->editing = 0;
                break;
            case KEY_BACKSPACE:
            case 127:
                if (s->cursor_pos > 0) {
                    memmove(&s->script_text[s->cursor_pos - 1],
                            &s->script_text[s->cursor_pos],
                            strlen(&s->script_text[s->cursor_pos]) + 1);
                    s->cursor_pos--;
                }
                break;
            case '\n':
                memmove(&s->script_text[s->cursor_pos + 1],
                        &s->script_text[s->cursor_pos],
                        strlen(&s->script_text[s->cursor_pos]) + 1);
                s->script_text[s->cursor_pos++] = '\n';
                break;
            case KEY_LEFT:
                if (s->cursor_pos > 0)
                    s->cursor_pos--;
                break;
            case KEY_RIGHT:
                if (s->cursor_pos < (int)strlen(s->script_text))
                    s->cursor_pos++;
                break;
            case KEY_UP:
            case KEY_DOWN: {
                int cur_line2 = 0, cur_col2 = 0;
                for (int i = 0; i < s->cursor_pos; i++) {
                    if (s->script_text[i] == '\n') {
                        cur_line2++;
                        cur_col2 = 0;
                    } else
                        cur_col2++;
                }
                int target_line = cur_line2 + (ch == KEY_DOWN ? 1 : -1);
                if (target_line < 0)
                    target_line = 0;
                int i = 0, l = 0, c = 0, target_pos = s->cursor_pos;
                while (s->script_text[i]) {
                    if (l == target_line && c == cur_col2) {
                        target_pos = i;
                        break;
                    }
                    if (s->script_text[i] == '\n') {
                        l++;
                        c = 0;
                    } else
                        c++;
                    i++;
                }
                s->cursor_pos = target_pos;
                break;
            }
            default:
                if (ch >= 32 && ch < 127 &&
                    s->cursor_pos < (int)sizeof(s->script_text) - 1) {
                    memmove(&s->script_text[s->cursor_pos + 1],
                            &s->script_text[s->cursor_pos],
                            strlen(&s->script_text[s->cursor_pos]) + 1);
                    s->script_text[s->cursor_pos++] = (char)ch;
                }
                break;
            }
            pthread_mutex_unlock(&s->lock);
        }

        curs_set(0);
        nodelay(stdscr, TRUE);
        return;
    }

    /* ---- Ctrl-R = run (or stop, if script is empty) ---- */
    if (!s->editing && key == 18) {
        run_script(s);
    }
}

static void lua_box_destroy(Module *m) {
    LuaBox *s = (LuaBox *)m->state;
    stop_loop(s);
    lua_state_close(s);
    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module *create_module(const char *args, float sample_rate) {
    (void)args;
    LuaBox *s = (LuaBox *)calloc(1, sizeof(LuaBox));
    s->sample_rate = sample_rate;
    pthread_mutex_init(&s->lock, NULL);
    lua_state_open(s);
    snprintf(s->last_result, sizeof(s->last_result), "ready");

    Module *m = (Module *)calloc(1, sizeof(Module));
    m->name = "lua_box";
    m->type = "lua_box";
    m->state = s;
    m->draw_ui = lua_box_draw_ui;
    m->handle_input = lua_box_handle_input;
    m->process_control = lua_box_process_control;
    m->destroy = lua_box_destroy;

    keypad(stdscr, TRUE);
    nodelay(stdscr, FALSE);
    curs_set(1);

    return m;
}
