#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_clock_s.h"
#include "module.h"
#include "util.h"

// Global shared clock registry
#define MAX_CLOCKS 64

static CClockS* g_clocks[MAX_CLOCKS];
static int          g_clock_count = 0;
static pthread_mutex_t g_clocks_lock = PTHREAD_MUTEX_INITIALIZER;

// Register/unregister helpers (called from create/destroy)
static void register_clock(CClockS* s) {
    pthread_mutex_lock(&g_clocks_lock);
    if (g_clock_count < MAX_CLOCKS) {
        g_clocks[g_clock_count++] = s;
    }
    pthread_mutex_unlock(&g_clocks_lock);
}

static void unregister_clock(CClockS* s) {
    pthread_mutex_lock(&g_clocks_lock);
    for (int i = 0; i < g_clock_count; ++i) {
        if (g_clocks[i] == s) {
            // compact
            for (int j = i; j < g_clock_count - 1; ++j)
                g_clocks[j] = g_clocks[j + 1];
            g_clock_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_clocks_lock);
}

// When primary BPM changes we want ALL clock phases to reset and
// share the new BPM immediately.
static void propagate_bpm_and_reset(float new_bpm) {
    pthread_mutex_lock(&g_clocks_lock);
    for (int i = 0; i < g_clock_count; ++i) {
        CClockS* c = g_clocks[i];
        if (!c) continue;

        pthread_mutex_lock(&c->lock);
        c->bpm           = new_bpm;
        c->display_bpm   = new_bpm;
        c->phase         = 0.0;
        c->last_gate     = 0.0f;
        pthread_mutex_unlock(&c->lock);
    }
    pthread_mutex_unlock(&g_clocks_lock);
}

static void propagate_run_and_reset(int running) {
    pthread_mutex_lock(&g_clocks_lock);
    for (int i = 0; i < g_clock_count; ++i) {
        CClockS* c = g_clocks[i];
        if (!c) continue;

        pthread_mutex_lock(&c->lock);
        c->running         = running;
        c->display_running = running && c->user_enable;
        c->phase           = 0.0;
        c->last_gate       = 0.0f;
        pthread_mutex_unlock(&c->lock);
    }
    pthread_mutex_unlock(&g_clocks_lock);
}

static void clamp_params(CClockS* s) {
    clampf(&s->bpm,  1.0f,    1000.0f);
    clampf(&s->mult, 0.0001f, 128.0f);
    clampf(&s->pw,   0.001f,  0.999f);
}

static void c_clock_process_control(Module* m, unsigned long frames) {
    CClockS* s = (CClockS*)m->state;

    float* out = m->control_output;
    if (!out) return;

    int has_sync = (m->num_control_inputs > 0);

    float* sync_buf = NULL;
    if (has_sync && m->num_control_inputs > 0)
        sync_buf = m->control_inputs[0];

    float  bpm, mult, pw;
    int    running, user_enable;
    double phase;
    float  sr;
    int    pending_resync;
    float  last_sync_in;

    pthread_mutex_lock(&s->lock);
    bpm           = s->bpm;
    mult          = s->mult;
    pw            = s->pw;
    running       = s->running;    
    user_enable   = s->user_enable; 
    phase         = s->phase;
    sr            = s->sample_rate;
    pending_resync = s->pending_resync;
    last_sync_in   = s->last_sync_in;
    pthread_mutex_unlock(&s->lock);

    float disp_bpm  = bpm;
    float disp_mult = mult;
    float disp_pw   = pw;
    float last_gate = s->last_gate;

    int effective_running = has_sync ? (running && user_enable) : running;

    if (!running) {
        for (unsigned long i = 0; i < frames; ++i)
            out[i] = 0.0f;

        pthread_mutex_lock(&s->lock);
        s->last_gate       = 0.0f;
        s->display_bpm     = disp_bpm;
        s->display_mult    = disp_mult;
        s->display_pw      = disp_pw;
        s->display_running = 0;
        pthread_mutex_unlock(&s->lock);
        return;
    }

    if (has_sync && !user_enable) {
        double base_freq = (double)bpm / 60.0;
        double freq      = base_freq * (double)mult;
        double phase_inc = freq / (double)sr;

        for (unsigned long i = 0; i < frames; ++i) {
            // Rising-edge detect from primary gate
            if (sync_buf) {
                float s_in = sync_buf[i];
                if (pending_resync && last_sync_in <= 0.5f && s_in > 0.5f) {
                    // snap to primary pulse
                    phase = 0.0;
                    pending_resync = 0;
                }
                last_sync_in = s_in;
            }

            phase += phase_inc;
            if (phase >= 1.0)
                phase -= floor(phase);

            out[i] = 0.0f;  // muted gate
        }

        pthread_mutex_lock(&s->lock);
        s->phase           = phase;
        s->last_gate       = 0.0f;
        s->display_bpm     = disp_bpm;
        s->display_mult    = disp_mult;
        s->display_pw      = disp_pw;
        s->display_running = 0;     // UI OFF while muted
        s->pending_resync  = pending_resync;
        s->last_sync_in    = last_sync_in;
        pthread_mutex_unlock(&s->lock);
        return;
    }

    double base_freq = (double)bpm / 60.0;
    double freq      = base_freq * (double)mult;

    if (freq <= 0.0) {
        for (unsigned long i = 0; i < frames; ++i)
            out[i] = 0.0f;

        pthread_mutex_lock(&s->lock);
        s->last_gate       = 0.0f;
        s->display_bpm     = disp_bpm;
        s->display_mult    = disp_mult;
        s->display_pw      = disp_pw;
        s->display_running = effective_running;
        s->pending_resync  = pending_resync;
        s->last_sync_in    = last_sync_in;
        pthread_mutex_unlock(&s->lock);
        return;
    }

    double phase_inc = freq / (double)sr;

    for (unsigned long i = 0; i < frames; ++i) {
        // For secondaries, optionally resync phase on next primary pulse
        if (has_sync && sync_buf) {
            float s_in = sync_buf[i];
            if (pending_resync && last_sync_in <= 0.5f && s_in > 0.5f) {
                phase = 0.0;        // lock to primary pulse
                pending_resync = 0;
            }
            last_sync_in = s_in;
        }

        phase += phase_inc;
        if (phase >= 1.0)
            phase -= floor(phase);

        float gate = (phase < pw) ? 1.0f : 0.0f;
        out[i]     = gate;
        last_gate  = gate;
    }

    pthread_mutex_lock(&s->lock);
    s->phase           = phase;
    s->last_gate       = last_gate;
    s->display_bpm     = disp_bpm;
    s->display_mult    = disp_mult;
    s->display_pw      = disp_pw;
    s->display_running = effective_running;
    s->pending_resync  = pending_resync;
    s->last_sync_in    = last_sync_in;
    pthread_mutex_unlock(&s->lock);
}

static void c_clock_draw_ui(Module* m, int y, int x) {
    CClockS* s = (CClockS*)m->state;

    pthread_mutex_lock(&s->lock);
    float bpm     = s->display_bpm;
    float mult    = s->display_mult;
    float pw      = s->display_pw;
    float gate    = s->last_gate;
    int   running = s->display_running;
    char  buf[64]; memcpy(buf, s->command_buffer, sizeof(buf));
    pthread_mutex_unlock(&s->lock);

    BLUE();   mvprintw(y,   x, "[CLK:%s] ", m->name); CLR();
    LABEL(2, "bpm:");  ORANGE(); printw(" %.1f | ", bpm);  CLR();
    LABEL(2, "mult:"); ORANGE(); printw(" %.2f | ", mult); CLR();
    LABEL(2, "pw:");   ORANGE(); printw(" %.2f | ", pw);   CLR();
    LABEL(2, "gate:"); ORANGE(); printw(" %d | ", (int)gate); CLR();
    LABEL(2, "run:");  ORANGE(); printw(" %s", running ? "on" : "off"); CLR();

    YELLOW();
    mvprintw(y+1, x, "Keys: -/= bpm,  _/+ mult,  [/] pw, SPACE run/stop");
    mvprintw(y+2, x, "Cmd: :1 [bpm], :2 [mult], :3 [pw]");
    CLR();
}

static void c_clock_handle_input(Module* m, int key) {
    CClockS* s = (CClockS*)m->state;
    int handled = 0;

    int has_sync = (m->num_control_inputs > 0); // secondary if >0

    int   do_propagate_bpm = 0;
    int   do_propagate_run = 0;
    float new_bpm          = 0.0f;
    int   new_running      = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            // BPM changes only allowed on primary (no sync input)
            case '-':
                if (!has_sync) {
                    s->bpm -= 1.0f;
                    handled = 1;
                    do_propagate_bpm = 1;
                    new_bpm = s->bpm;
                }
                break;
            case '=':
                if (!has_sync) {
                    s->bpm += 1.0f;
                    handled = 1;
                    do_propagate_bpm = 1;
                    new_bpm = s->bpm;
                }
                break;

            // mult / pw always local
            case '_':
                s->mult *= 0.5f;
				if (has_sync) s->pending_resync = 1;
                handled = 1;
                break;
            case '+':
                s->mult *= 2.0f;
				if (has_sync) s->pending_resync = 1;
                handled = 1;
                break;
            case '[':
                s->pw -= 0.01f;
                handled = 1;
                break;
            case ']':
                s->pw += 0.01f;
                handled = 1;
                break;

            // SPACE: primary toggles global run, secondary toggles its user_enable
            case ' ':
                if (has_sync) {
                    s->user_enable = !s->user_enable;
                    handled = 1;
                    // No global run change here; display_running updated in process loop
                } else {
                    s->running = !s->running;
                    handled = 1;
                    do_propagate_run = 1;
                    new_running = s->running;
                }
                break;

            case ':':
                s->entering_command = true;
                memset(s->command_buffer, 0, sizeof(s->command_buffer));
                s->command_index = 0;
                handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            s->entering_command = false;
            char type;
            float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
                if (type == '1') {
                    if (!has_sync) {
                        s->bpm = val;
                        do_propagate_bpm = 1;
                        new_bpm = s->bpm;
                    }
                } else if (type == '2') {
                    s->mult = val;
					if (has_sync) s->pending_resync = 1;
                } else if (type == '3') {
                    s->pw = val;
                }
            }
            handled = 1;
        } else if (key == 27) { // ESC
            s->entering_command = false;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        } else if (key >= 32 && key < 127 &&
                   s->command_index < (int)sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
    }

    if (handled)
        clamp_params(s);

    pthread_mutex_unlock(&s->lock);

    // Do propagation *outside* of lock to avoid deadlocks with audio thread.
    if (do_propagate_bpm)
        propagate_bpm_and_reset(new_bpm);
    if (do_propagate_run)
        propagate_run_and_reset(new_running);
}

static void c_clock_set_osc_param(Module* m, const char* param, float value) {
    CClockS* s = (CClockS*)m->state;
    int has_sync = (m->num_control_inputs > 0);

    int   do_propagate_bpm = 0;
    int   do_propagate_run = 0;
    float new_bpm          = 0.0f;
    int   new_running      = 0;

    pthread_mutex_lock(&s->lock);

    if (strcmp(param, "bpm") == 0) {
        // Only primary can change master BPM
        if (!has_sync) {
            s->bpm = value;
            do_propagate_bpm = 1;
            new_bpm = s->bpm;
        }
    } else if (strcmp(param, "mult") == 0) {
        s->mult = value;
		if (has_sync) s->pending_resync = 1;
    } else if (strcmp(param, "pw") == 0) {
        s->pw = value;
    } else if (strcmp(param, "run") == 0) {
        if (!has_sync) {
            s->running = (value > 0.5f);
            do_propagate_run = 1;
            new_running = s->running;
        } else {
            // Secondary: treat "run" as local enable
            s->user_enable = (value > 0.5f);
        }
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);

    if (do_propagate_bpm)
        propagate_bpm_and_reset(new_bpm);
    if (do_propagate_run)
        propagate_run_and_reset(new_running);
}

static void c_clock_destroy(Module* m) {
    if (!m) return;
    CClockS* s = (CClockS*)m->state;
    if (s) {
        unregister_clock(s);
        pthread_mutex_destroy(&s->lock);
    }
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float bpm  = 120.0f;
    float mult = 1.0f;
    float pw   = 0.5f;

    if (args && strstr(args, "bpm="))  sscanf(strstr(args, "bpm="),  "bpm=%f",  &bpm);
    if (args && strstr(args, "mult=")) sscanf(strstr(args, "mult="), "mult=%f", &mult);
    if (args && strstr(args, "pw="))   sscanf(strstr(args, "pw="),   "pw=%f",   &pw);

    CClockS* s = calloc(1, sizeof(CClockS));
    s->bpm            = bpm;
    s->mult           = mult;
    s->pw             = pw;
    s->last_gate      = 0.0f;
    s->phase          = 0.0;
    s->sample_rate    = sample_rate;

    s->running        = 1;
    s->user_enable    = 1;

	s->pending_resync = 0;
	s->last_sync_in   = 0.0f;

    s->display_bpm    = bpm;
    s->display_mult   = mult;
    s->display_pw     = pw;
    s->display_running = 1;

    s->entering_command = false;
    s->command_index    = 0;
    memset(s->command_buffer, 0, sizeof(s->command_buffer));

    pthread_mutex_init(&s->lock, NULL);
    register_clock(s);

    Module* m = calloc(1, sizeof(Module));
    m->name            = "c_clock";
    m->state           = s;
    m->process_control = c_clock_process_control;
    m->draw_ui         = c_clock_draw_ui;
    m->handle_input    = c_clock_handle_input;
    m->set_param       = c_clock_set_osc_param;
    m->destroy         = c_clock_destroy;
    m->control_output  = calloc(MAX_BLOCK_SIZE, sizeof(float));
    return m;
}

