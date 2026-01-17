// c_logic.c — control-rate logic gate (Signal Crate compliant)

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>

#include "c_logic.h"
#include "module.h"
#include "util.h"

#define LOGIC_THRESH 0.5f

static inline float logic_eval(LogicType t, float a, float b) {
    int A = (a > LOGIC_THRESH);
    int B = (b > LOGIC_THRESH);

    switch (t) {
        case AND:  return (A && B) ? 1.0f : 0.0f;
        case OR:   return (A || B) ? 1.0f : 0.0f;
        case XOR:  return (A ^  B) ? 1.0f : 0.0f;
        case NAND: return (!(A && B)) ? 1.0f : 0.0f;
        case NOR:  return (!(A || B)) ? 1.0f : 0.0f;
        case XNOR: return (!(A ^  B)) ? 1.0f : 0.0f;
        case NOT:  return (!A) ? 1.0f : 0.0f;   // input 1 only
        default:   return 0.0f;
    }
}

static void c_logic_process_control(Module* m, unsigned long frames) {
    CLogic* s = (CLogic*)m->state;

    float* in1 = (m->num_control_inputs > 0) ? m->control_inputs[0] : NULL;
    float* in2 = (m->num_control_inputs > 1) ? m->control_inputs[1] : NULL;
    float* out = m->control_output;

    LogicType type;
    pthread_mutex_lock(&s->lock);
    type = s->logic_type;
    pthread_mutex_unlock(&s->lock);

    float last1 = 0.0f, last2 = 0.0f;

    for (unsigned long i = 0; i < frames; i++) {
        float a = in1 ? in1[i] : 0.0f;
        float b = in2 ? in2[i] : 0.0f;
        last1 = a;
        last2 = b;
        out[i] = logic_eval(type, a, b);
    }

    pthread_mutex_lock(&s->lock);
    s->display_in1 = last1;
    s->display_in2 = last2;
    pthread_mutex_unlock(&s->lock);
}

static const char* logic_name(LogicType t) {
    switch (t) {
        case AND:  return "AND";
        case OR:   return "OR";
        case XOR:  return "XOR";
        case NAND: return "NAND";
        case NOR:  return "NOR";
        case XNOR: return "XNOR";
        case NOT:  return "NOT";
        default:   return "?";
    }
}

static void c_logic_draw_ui(Module* m, int y, int x) {
    CLogic* s = (CLogic*)m->state;

    float d1, d2;
    LogicType t;
    int cmd;

    pthread_mutex_lock(&s->lock);
    d1 = s->display_in1;
    d2 = s->display_in2;
    t  = s->logic_type;
    cmd = s->entering_command;
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[c_logic] > 0.5v in trig ");
    CLR();

    LABEL(2, "type:");
    ORANGE(); printw(" %s | ", logic_name(t)); CLR();

    LABEL(2, "in1:");
    ORANGE(); printw(" %.2f | ", d1); CLR();

    LABEL(2, "in2:");
    ORANGE(); printw(" %.2f ", d2); CLR();

    YELLOW();
    mvprintw(y + 1, x, "Keys: l=logic | :LOGIC (ie. :AND :OR :XOR ...)");
    if (cmd) mvprintw(y + 2, x, ":%s", s->command_buffer);
    BLACK();
}

static LogicType parse_logic(const char* s, LogicType cur) {
    if (!s) return cur;
    if (!strcasecmp(s, "AND"))  return AND;
    if (!strcasecmp(s, "OR"))   return OR;
    if (!strcasecmp(s, "XOR"))  return XOR;
    if (!strcasecmp(s, "NAND")) return NAND;
    if (!strcasecmp(s, "NOR"))  return NOR;
    if (!strcasecmp(s, "XNOR")) return XNOR;
    if (!strcasecmp(s, "NOT"))  return NOT;
    return cur;
}

static void c_logic_handle_input(Module* m, int key) {
    CLogic* s = (CLogic*)m->state;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case 'l': 
            case 'L':
                s->logic_type = (LogicType)(((int)s->logic_type + 1) % 7);
                break;

            case ':':
                s->entering_command = true;
                s->command_index = 0;
                memset(s->command_buffer, 0, sizeof(s->command_buffer));
                break;
        }
    } else {
        if (key == '\n') {
            s->logic_type = parse_logic(s->command_buffer, s->logic_type);
            s->entering_command = false;
        } else if (key == 27) {
            s->entering_command = false;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
        } else if (key >= 32 && key < 127 &&
                   s->command_index < (int)sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
        }
    }

    pthread_mutex_unlock(&s->lock);
}

static void c_logic_set_param(Module* m, const char* param, float value) {
    CLogic* s = (CLogic*)m->state;
    pthread_mutex_lock(&s->lock);

    if (!strcmp(param, "type")) {
        int idx = (int)(fminf(fmaxf(value, 0.0f), 1.0f) * 6.999f);
        s->logic_type = (LogicType)idx;
    }

    pthread_mutex_unlock(&s->lock);
}

static void c_logic_destroy(Module* m) {
    CLogic* s = (CLogic*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    (void)sample_rate;

    LogicType type = AND;
    if (args && strstr(args, "type=")) {
        char tmp[16];
        if (sscanf(strstr(args, "type="), "type=%15s", tmp) == 1)
            type = parse_logic(tmp, type);
    }

    CLogic* s = calloc(1, sizeof(CLogic));
    s->logic_type = type;
    pthread_mutex_init(&s->lock, NULL);

    Module* m = calloc(1, sizeof(Module));
    m->name = "c_logic";
    m->state = s;
    m->process_control = c_logic_process_control;
    m->draw_ui = c_logic_draw_ui;
    m->handle_input = c_logic_handle_input;
    m->set_param = c_logic_set_param;
    m->control_output = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->destroy = c_logic_destroy;
    return m;
}

