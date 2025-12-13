#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_random.h"
#include "module.h"
#include "util.h"

static inline void clamp_params(CRandom* s) {
    clampf(&s->rate_hz, 0.01f, 100.0f);
    clampf(&s->depth,   0.0f, 1.0f);
}

static void c_random_process_control(Module* m) {
    CRandom* s = (CRandom*)m->state;

    float rate, rmin, rmax, depth;
    RandomType type;

    pthread_mutex_lock(&s->lock);
    float raw_rate  = s->rate_hz;
    float raw_depth = s->depth;
    rmin            = s->range_min;
    rmax            = s->range_max;
    type            = s->type;
    pthread_mutex_unlock(&s->lock);

    rate  = process_smoother(&s->smooth_rate,  raw_rate);
    depth = process_smoother(&s->smooth_depth, raw_depth);

    // Clamp smoothed values
    if (rate < 0.01f) rate = 0.01f;
    if (rate > 100.0f) rate = 100.0f;
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;

    float* out = m->control_output;
    if (!out) return;

    float dt = 1.0f / s->sample_rate;

    for (int i = 0; i < MAX_BLOCK_SIZE; i++) {

        s->phase += dt * rate;
        if (s->phase >= 1.0f) {
            s->phase -= 1.0f;

            float base = ((float)rand() / RAND_MAX) * 2.0f - 1.0f;

            float shaped = base;
            switch (type) {
                case RAND_WHITE: break;
                case RAND_PINK:  shaped = pink_filter_process(&s->pink, base); break;
                case RAND_BROWN: shaped = brown_noise_process(&s->brown, base); break;
            }

            if (shaped > 1.0f) shaped = 1.0f;
            if (shaped < -1.0f) shaped = -1.0f;

            float u = (shaped + 1.0f) * 0.5f;

            float u_depth = 0.5f + (u - 0.5f) * depth;

            float u_range = rmin + u_depth * (rmax - rmin);

            float final = fminf(fmaxf(u_range, 0.0f), 1.0f);
            s->current_val = final;

            pthread_mutex_lock(&s->lock);
            s->display_val = final;
            pthread_mutex_unlock(&s->lock);
        }

        out[i] = s->current_val;
    }
}

static void c_random_draw_ui(Module* m, int y, int x) {
    CRandom* s = (CRandom*)m->state;

    float val, rate, rmin, rmax, depth;
    RandomType type;

    pthread_mutex_lock(&s->lock);
    val   = s->display_val;
    rate  = s->rate_hz;
    rmin  = s->range_min;
    rmax  = s->range_max;
    depth = s->depth;
    type  = s->type;
    pthread_mutex_unlock(&s->lock);

    const char* names[] = {"w","p","b"};

    BLUE();
    mvprintw(y, x, "[Random:%s] ", m->name);
    CLR();

    LABEL(2,"r:");   ORANGE(); printw(" %.2f Hz | ", rate); CLR();
    LABEL(2,"rng:");  ORANGE(); printw(" %.2f-%.2f | ", rmin, rmax); CLR();
    LABEL(2,"type:");   ORANGE(); printw(" %s | ", names[type]); CLR();
    LABEL(2,"d:");  ORANGE(); printw(" %.2f | ", depth); CLR();
    LABEL(2,"v:");    ORANGE(); printw(" %.3f", val);   CLR();

    YELLOW();
    mvprintw(y+1,x, "-/= rate, [/] min, {/} max, n (type), d/D depth");
    mvprintw(y+2,x, "Cmd: :1 rate, :2 type, :3 rmin, :4 rmax :d depth");
    BLACK();
}

static void c_random_handle_input(Module* m, int key) {
    CRandom* s = (CRandom*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->rate_hz += 0.1f; handled=1; break;
            case '-': s->rate_hz -= 0.1f; handled=1; break;
            case '[': s->range_min -= 0.01f; handled=1; break;
            case ']': s->range_min += 0.01f; handled=1; break;
            case '{': s->range_max -= 0.01f; handled=1; break;
            case '}': s->range_max += 0.01f; handled=1; break;
            case 'n': s->type = (s->type + 1) % 3; handled=1; break;
            case 'D': s->depth += 0.01f; handled=1; break;
            case 'd': s->depth -= 0.01f; handled=1; break;
            case ':':
                s->entering_command = true;
                memset(s->command_buffer,0,sizeof(s->command_buffer));
                s->command_index = 0;
                handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            s->entering_command = false;
            char t;
            float v;
            if (sscanf(s->command_buffer, "%c %f", &t, &v) == 2) {
                if (t=='1') s->rate_hz = v;
                else if (t=='2') s->range_min = v;
                else if (t=='3') s->range_max = v;
                else if (t=='4') s->type = (RandomType)((int)v % 3);
                else if (t=='d') s->depth = v;
            }
            handled = 1;
        }
        else if (key == 27) {
            s->entering_command = false;
            handled = 1;
        }
        else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled = 1;
        }
        else if (key >= 32 && key < 127 && s->command_index < (int)sizeof(s->command_buffer)-1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled=1;
        }
    }

    if (handled)
        clamp_params(s);

    pthread_mutex_unlock(&s->lock);
}

static void c_random_set_osc_param(Module* m, const char* param, float value) {
    CRandom* s = (CRandom*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param,"rate")==0)
        s->rate_hz = value;
    else if (strcmp(param,"depth")==0)
        s->depth = value;
    else if (strcmp(param,"type")==0)
        s->type = (RandomType)(((int)value) % 3);
    else if (strcmp(param,"rmin")==0)
        s->range_min = value;
    else if (strcmp(param,"rmax")==0)
        s->range_max = value;

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void c_random_destroy(Module* m) {
    CRandom* s = (CRandom*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    float rate = 1.0f;
    float rmin = 0.0f;
    float rmax = 1.0f;
    float depth = 1.0f;
    RandomType type = RAND_WHITE;

    if (args && strstr(args,"rate="))
        sscanf(strstr(args,"rate="),"rate=%f",&rate);
    if (args && strstr(args,"depth="))
        sscanf(strstr(args,"depth="),"depth=%f",&depth);
    if (args && strstr(args,"rmin="))
        sscanf(strstr(args,"rmin="),"rmin=%f",&rmin);
    if (args && strstr(args,"rmax="))
        sscanf(strstr(args,"rmax="),"rmax=%f",&rmax);
    if (args && strstr(args,"type=")) {
        char buf[32]={0};
        sscanf(strstr(args,"type="),"type=%31s",buf);
        if (strcmp(buf,"white")==0) type=RAND_WHITE;
        else if (strcmp(buf,"pink")==0)  type=RAND_PINK;
        else if (strcmp(buf,"brown")==0) type=RAND_BROWN;
    }

    CRandom* s = calloc(1,sizeof(CRandom));
    s->rate_hz = rate;
    s->depth   = depth;
    s->range_min = rmin;
    s->range_max = rmax;
    s->type = type;
    s->sample_rate = sample_rate;
    s->phase = 0.0f;

    pink_filter_init(&s->pink, sample_rate);
    brown_noise_init(&s->brown);

    pthread_mutex_init(&s->lock,NULL);
    init_smoother(&s->smooth_rate,  0.75f);
    init_smoother(&s->smooth_depth, 0.75f);

    clamp_params(s);

    Module* m = calloc(1,sizeof(Module));
    m->name = "c_random";
    m->state = s;
    m->control_output = calloc(MAX_BLOCK_SIZE,sizeof(float));
    m->process_control = c_random_process_control;
    m->draw_ui = c_random_draw_ui;
    m->handle_input = c_random_handle_input;
    m->set_param = c_random_set_osc_param;
    m->destroy = c_random_destroy;

    return m;
}

