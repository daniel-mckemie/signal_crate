#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "bark_bank.h"
#include "module.h"
#include "util.h"

static float bark_centers[BARK_BANDS] = {
    80,120,180,260,360,510,720,1000,
    1400,2000,2800,3700,
    4800,6200,8000,10000,
    12000,14000,16000,18000,
    20000,22000,24000,26000
};

static inline float tilt_gain(int i, float tilt) {
    float t = (float)i / (float)(BARK_BANDS - 1);
    return powf(2.0f, tilt * (t - 0.5f) * 4.0f);
}

static inline float center_window(int i, float center, float width) {
    float x = (float)i / (float)(BARK_BANDS - 1);
    float d = x - center;
    float w = fmaxf(width, 0.02f);
    return expf(-(d*d) / (2.0f*w*w));
}

static inline float soft_sat(float x, float drive) {
    float g = 1.0f + 9.0f * drive;
    float y = g * x;
    return y / (1.0f + fabsf(y));
}

static inline float biquad_tick(BarkBank* s, int b, int st, float x) {
    float y = s->b0[b][st]*x + s->z1[b][st];
    s->z1[b][st] = s->b1[b][st]*x + s->z2[b][st] - s->a1[b][st]*y;
    s->z2[b][st] = s->b2[b][st]*x - s->a2[b][st]*y;
    return y;
}

static void rebuild_filters(BarkBank* s) {
    float ny = s->sample_rate * 0.45f;

    for (int i=0;i<BARK_BANDS;i++) {
        float fc = bark_centers[i];
        if (fc > ny) fc = ny;

        s->fc[i] = fc;
        s->Q[i]  = 6.0f;

        for (int st=0; st<BARK_STAGES; st++) {
            float omega = TWO_PI * fc / s->sample_rate;
            float sn = sinf(omega), cs = cosf(omega);
            float alpha = sn / (2.0f * s->Q[i]);

            float b0 =  alpha;
            float b1 =  0.0f;
            float b2 = -alpha;
            float a0 =  1.0f + alpha;
            float a1 = -2.0f * cs;
            float a2 =  1.0f - alpha;

            s->b0[i][st] = b0 / a0;
            s->b1[i][st] = b1 / a0;
            s->b2[i][st] = b2 / a0;
            s->a1[i][st] = a1 / a0;
            s->a2[i][st] = a2 / a0;
        }
    }
}

static void clamp_params(BarkBank* s) {
    clampf(&s->mix,   0.0f, 1.0f);
    clampf(&s->center,0.0f, 1.0f);
    clampf(&s->width, 0.02f, 1.0f);
    clampf(&s->tilt, -1.0f, 1.0f);
    clampf(&s->drive, 0.0f, 1.0f);

    if (s->even_odd < 0) s->even_odd = 0;
    if (s->even_odd > 2) s->even_odd = 2;

    if (s->sel_band < 0) s->sel_band = 0;
    if (s->sel_band > BARK_BANDS-1) s->sel_band = BARK_BANDS-1;

    for (int i=0;i<BARK_BANDS;i++)
        clampf(&s->band_gain[i], 0.0f, 2.0f);
}

static void bark_bank_process(Module* m, float* in, unsigned long frames) {
    BarkBank* s = (BarkBank*)m->state;
    float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
    float* out   = m->output_buffer;

    pthread_mutex_lock(&s->lock);
    float base_mix    = s->mix;
    float base_center = s->center;
    float base_width  = s->width;
    float base_tilt   = s->tilt;
    float base_drive  = s->drive;
    int   base_eo     = s->even_odd;
    pthread_mutex_unlock(&s->lock);

    float mix_s    = process_smoother(&s->smooth_mix, base_mix);
    float center_s = process_smoother(&s->smooth_center, base_center);
    float width_s  = process_smoother(&s->smooth_width, base_width);
    float tilt_s   = process_smoother(&s->smooth_tilt, base_tilt);
    float drive_s  = process_smoother(&s->smooth_drive, base_drive);

    float disp_mix = mix_s;
    float disp_center = center_s;
    float disp_width = width_s;
    float disp_tilt = tilt_s;
    float disp_drive = drive_s;
    int   disp_eo = base_eo;

    for (unsigned int i=0;i<frames;i++) {
        float mix    = mix_s;
        float center = center_s;
        float width  = width_s;
        float tilt   = tilt_s;
        float drive  = drive_s;
        int eo       = base_eo;

        for (int j=0; j<m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param,"mix")==0) mix += control;
            else if (strcmp(param,"center")==0) center += control;
            else if (strcmp(param,"width")==0) width += control;
            else if (strcmp(param,"tilt")==0) tilt += control;
            else if (strcmp(param,"drive")==0) drive += control;
            else if (strcmp(param,"evenodd")==0) eo += (int)lrintf(control);
            else if (param[0]=='b') {
                int idx = atoi(param+1);
                if (idx >= 0 && idx < BARK_BANDS) {
                    float g = s->band_gain[idx] + control;
                    clampf(&g, 0.0f, 2.0f);
                    s->band_gain[idx] = g;
                }
            }
        }

        clampf(&mix, 0.0f, 1.0f);
        clampf(&center, 0.0f, 1.0f);
        clampf(&width, 0.02f, 1.0f);
        clampf(&tilt, -1.0f, 1.0f);
        clampf(&drive, 0.0f, 1.0f);
        if (eo < 0) eo = 0;
        if (eo > 2) eo = 2;

        disp_mix = mix;
        disp_center = center;
        disp_width = width;
        disp_tilt = tilt;
        disp_drive = drive;
        disp_eo = eo;

        float x = (input ? input[i] : 0.0f) + 1e-20f;
        if (!isfinite(x)) x = 0.0f;

        float sum = 0.0f;

        for (int b=0;b<BARK_BANDS;b++) {
            if (eo==1 && (b&1)) continue;
            if (eo==2 && !(b&1)) continue;

            float y = x;
            for (int st=0; st<BARK_STAGES; st++)
                y = biquad_tick(s,b,st,y);

            float g = process_smoother(&s->smooth_band[b], s->band_gain[b]);
            float w = center_window(b, center, width);
            float t = tilt_gain(b, tilt);

            sum += y * g * w * t;
        }

        float wet = soft_sat(sum, drive);
        float yout = mix * wet + (1.0f - mix) * x;

        if (!isfinite(yout)) yout = 0.0f;
        out[i] = fminf(fmaxf(yout, -1.0f), 1.0f);
    }

    pthread_mutex_lock(&s->lock);
    s->display_mix = disp_mix;
    s->display_center = disp_center;
    s->display_width = disp_width;
    s->display_tilt = disp_tilt;
    s->display_drive = disp_drive;
    s->display_even_odd = disp_eo;
    s->display_sel_gain = s->band_gain[s->sel_band];
    pthread_mutex_unlock(&s->lock);
}

static void bark_bank_draw_ui(Module* m, int y, int x) {
    BarkBank* s = (BarkBank*)m->state;
    float mix,center,width,tilt,drive; int eo; int sb; float sg;
    char cmd[64] = "";

    pthread_mutex_lock(&s->lock);
    mix = s->display_mix;
    center = s->display_center;
    width = s->display_width;
    tilt = s->display_tilt;
    drive = s->display_drive;
    eo = s->display_even_odd;
    sb = s->sel_band;
    sg = s->display_sel_gain;
    if (s->entering_command) snprintf(cmd, sizeof(cmd), ":%s", s->command_buffer);
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[BarkBank:%s] ", m->name);
    CLR();

    LABEL(2,"mix:"); ORANGE(); printw("%.2f", mix); CLR();
    LABEL(2,"ctr:"); ORANGE(); printw("%.2f", center); CLR();
    LABEL(2,"wid:"); ORANGE(); printw("%.2f", width); CLR();
    LABEL(2,"tilt:"); ORANGE(); printw("%.2f", tilt); CLR();
    LABEL(2,"drv:"); ORANGE(); printw("%.2f", drive); CLR();
    LABEL(2,"eo:"); ORANGE(); printw("%d", eo); CLR();
    LABEL(2,"b:"); ORANGE(); printw("%02d", sb); CLR();
    LABEL(2,"g:"); ORANGE(); printw("%.2f", sg); CLR();

    YELLOW();
    mvprintw(y+1, x, "-/= mix  _/+ ctr [/] wid  {/} tilt  ;/' drv  e eo  b/B band  </> gain");
    mvprintw(y+2, x, ":1[mix] :2[ctr] :3[wid] :4[tilt] :5[drv] :6[eo] :7[band] :8[gain]");
    BLACK();
}

static void bark_bank_handle_input(Module* m, int key) {
    BarkBank* s = (BarkBank*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);
    if (!s->entering_command) {
        switch (key) {
            case '=': s->mix += 0.01f; handled=1; break;
            case '-': s->mix -= 0.01f; handled=1; break;
            case '+': s->center += 0.01f; handled=1; break;
            case '_': s->center -= 0.01f; handled=1; break;
            case ']': s->width += 0.01f; handled=1; break;
            case '[': s->width -= 0.01f; handled=1; break;
            case '}': s->tilt += 0.01f; handled=1; break;
            case '{': s->tilt -= 0.01f; handled=1; break;
            case '\'': s->drive += 0.01f; handled=1; break;
            case ';': s->drive -= 0.01f; handled=1; break;
            case 'e': s->even_odd = (s->even_odd + 1) % 3; handled=1; break;

			case 'B': s->sel_band += 1; handled=1; break;
            case 'b': s->sel_band -= 1; handled=1; break;
            case '>': s->band_gain[s->sel_band] += 0.01f; handled=1; break;
            case '<': s->band_gain[s->sel_band] -= 0.01f; handled=1; break;

            case ':':
                s->entering_command = true;
                memset(s->command_buffer, 0, sizeof(s->command_buffer));
                s->command_index = 0;
                handled=1;
                break;
        }
    } else {
        if (key == '\n') {
            s->entering_command = false;
            char type;
            float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
                if      (type=='1') s->mix = val;
                else if (type=='2') s->center = val;
                else if (type=='3') s->width = val;
                else if (type=='4') s->tilt = val;
                else if (type=='5') s->drive = val;
                else if (type=='6') s->even_odd = (int)val;
                else if (type=='7') s->sel_band = (int)val;
                else if (type=='8') s->band_gain[s->sel_band] = val;
            }
            handled=1;
        } else if (key == 27) {
            s->entering_command = false; handled=1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0';
            handled=1;
        } else if (key >= 32 && key < 127 && s->command_index < (int)sizeof(s->command_buffer)-1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0';
            handled=1;
        }
    }

    if (handled) {
        clamp_params(s);
    }
    pthread_mutex_unlock(&s->lock);
}

static void bark_bank_set_osc_param(Module* m, const char* param, float value) {
    BarkBank* s = (BarkBank*)m->state;
    pthread_mutex_lock(&s->lock);

    if      (strcmp(param,"mix")==0) s->mix = value;
    else if (strcmp(param,"center")==0) s->center = value;
    else if (strcmp(param,"width")==0) s->width = value;
    else if (strcmp(param,"tilt")==0) s->tilt = value;
    else if (strcmp(param,"drive")==0) s->drive = value;
    else if (strcmp(param,"evenodd")==0) s->even_odd = (int)value;
    else if (param[0]=='b') {
        int idx = atoi(param+1);
        if (idx >= 0 && idx < BARK_BANDS)
            s->band_gain[idx] = value;
    } else {
        /* no stderr spam */
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void bark_bank_destroy(Module* m) {
    BarkBank* s = (BarkBank*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    BarkBank* s = calloc(1, sizeof(BarkBank));
    s->sample_rate = sample_rate;

    s->mix = 0.5f;
    s->center = 0.5f;
    s->width = 0.35f;
    s->tilt = 0.0f;
    s->drive = 0.2f;
    s->even_odd = 0;
    s->sel_band = 0;

    for (int i=0;i<BARK_BANDS;i++) s->band_gain[i] = 1.0f;

    if (args && strstr(args,"mix="))    sscanf(strstr(args,"mix="),    "mix=%f", &s->mix);
    if (args && strstr(args,"center=")) sscanf(strstr(args,"center="), "center=%f", &s->center);
    if (args && strstr(args,"width="))  sscanf(strstr(args,"width="),  "width=%f", &s->width);
    if (args && strstr(args,"tilt="))   sscanf(strstr(args,"tilt="),   "tilt=%f", &s->tilt);
    if (args && strstr(args,"drive="))  sscanf(strstr(args,"drive="),  "drive=%f", &s->drive);
    if (args && strstr(args,"evenodd="))sscanf(strstr(args,"evenodd="),"evenodd=%d", &s->even_odd);

    pthread_mutex_init(&s->lock, NULL);

    init_smoother(&s->smooth_mix,   0.50f);
    init_smoother(&s->smooth_center,0.75f);
    init_smoother(&s->smooth_width, 0.75f);
    init_smoother(&s->smooth_tilt,  0.50f);
    init_smoother(&s->smooth_drive, 0.50f);
    for (int i=0;i<BARK_BANDS;i++) init_smoother(&s->smooth_band[i], 0.50f);

    clamp_params(s);
    rebuild_filters(s);

    s->display_mix = s->mix;
    s->display_center = s->center;
    s->display_width = s->width;
    s->display_tilt = s->tilt;
    s->display_drive = s->drive;
    s->display_even_odd = s->even_odd;
    s->display_sel_gain = s->band_gain[s->sel_band];

    Module* m = calloc(1, sizeof(Module));
    m->name = "bark_bank";
    m->state = s;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = bark_bank_process;
    m->draw_ui = bark_bank_draw_ui;
    m->handle_input = bark_bank_handle_input;
    m->set_param = bark_bank_set_osc_param;
    m->destroy = bark_bank_destroy;
    return m;
}

