#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "bark_processor.h"
#include "module.h"
#include "util.h"

static float bark_centers[BARK_PROC_BANDS] = {
    80,120,180,260,360,510,720,1000,
    1400,2000,2800,3700,
    4800,6200,8000,10000,
    12000,14000,16000,18000,
    20000,22000,24000,26000
};

static inline float tilt_gain(int i, float tilt) {
    float t = (float)i / (float)(BARK_PROC_BANDS - 1);
    return powf(2.0f, tilt * (t - 0.5f) * 4.0f);
}

static inline float center_window(int i, float center, float width) {
    float x = (float)i / (float)(BARK_PROC_BANDS - 1);
    float d = x - center;
    float w = fmaxf(width, 0.02f);
    return expf(-(d*d) / (2.0f*w*w));
}

static inline float soft_sat(float x, float drive) {
    float g = 1.0f + 9.0f * drive;
    float y = g * x;
    return y / (1.0f + fabsf(y));
}

static inline float coeff_from_ms(float ms, float sr) {
    if (ms <= 0.0f) return 1.0f;
    float tau = ms * 0.001f;
    float a = 1.0f - expf(-1.0f / (tau * sr));
    if (!isfinite(a)) a = 1.0f;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return a;
}

static inline float biquad_tick_state(
    const BarkProcessor* s,
    int b, int st,
    float x,
    float* z1, float* z2
) {
    float y = s->b0[b][st]*x + *z1;
    *z1 = s->b1[b][st]*x + *z2 - s->a1[b][st]*y;
    *z2 = s->b2[b][st]*x - s->a2[b][st]*y;
    return y;
}

static void rebuild_filters(BarkProcessor* s) {
    float ny = s->sample_rate * 0.45f;

    for (int i=0;i<BARK_PROC_BANDS;i++) {
        float fc = bark_centers[i];
        if (fc > ny) fc = ny;

        s->fc[i] = fc;
        s->Q[i]  = 1.2f;
		s->band_norm[i] = 1.0f / sqrtf(s->Q[i]);

        for (int st=0; st<BARK_PROC_STAGES; st++) {
            float omega = TWO_PI * fc / s->sample_rate;
            float sn = sinf(omega), cs = cosf(omega);
			float alpha = sn / (2.0f * s->Q[i]);

			float b0 =  sn * 0.5f;
			float b1 =  0.0f;
			float b2 = -sn * 0.5f;

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

static void clamp_params(BarkProcessor* s) {
    clampf(&s->center,0.0f, 1.0f);
    clampf(&s->width, 0.02f, 1.0f);
    clampf(&s->tilt, -1.0f, 1.0f);
    clampf(&s->drive, 0.0f, 1.0f);

    clampf(&s->attack_ms,  0.1f, 200.0f);
    clampf(&s->release_ms, 1.0f, 1000.0f);

    if (s->even_odd < 0) s->even_odd = 0;
    if (s->even_odd > 2) s->even_odd = 2;

    if (s->sel_band < 0) s->sel_band = 0;
    if (s->sel_band > BARK_PROC_BANDS-1) s->sel_band = BARK_PROC_BANDS-1;

    for (int i=0;i<BARK_PROC_BANDS;i++)
        clampf(&s->band_gain[i], 0.0f, 2.0f);
}

static int parse_band_gain_param(const char* param) {
    if (!param) return -1;

    /* b0..b23 */
    if (param[0]=='b') {
        int idx = atoi(param+1);
        if (idx >= 0 && idx < BARK_PROC_BANDS) return idx;
    }

    /* band1gain..band24gain */
    if (strncmp(param, "band", 4)==0) {
        const char* p = param + 4;
        int n = atoi(p); /* 1..24 */
        if (n >= 1 && n <= BARK_PROC_BANDS) {
            if (strstr(param, "gain")) return n - 1;
        }
    }

    return -1;
}

static void bark_processor_process(Module* m, float* in, unsigned long frames) {
    BarkProcessor* s = (BarkProcessor*)m->state;

    float* mod_in = (m->num_inputs > 0) ? m->inputs[0] : NULL;
    float* car_in = (m->num_inputs > 1) ? m->inputs[1] : NULL;
    float* out    = m->output_buffer;

    if (!mod_in || !car_in) {
		if (!mod_in) {
			printf("[ring_mod] WARNING: missing 2nd audio input\n");
		}
        memset(out, 0, frames * sizeof(float));
        return;
    }

    float base_band[BARK_PROC_BANDS];
    pthread_mutex_lock(&s->lock);
    float base_center = s->center;
    float base_width  = s->width;
    float base_tilt   = s->tilt;
    float base_drive  = s->drive;
    float base_atk_ms = s->attack_ms;
    float base_rel_ms = s->release_ms;
    int   base_eo     = s->even_odd;
    for (int b=0;b<BARK_PROC_BANDS;b++) base_band[b] = s->band_gain[b];
    pthread_mutex_unlock(&s->lock);

    float center_s = process_smoother(&s->smooth_center, base_center);
    float width_s  = process_smoother(&s->smooth_width, base_width);
    float tilt_s   = process_smoother(&s->smooth_tilt, base_tilt);
    float drive_s  = process_smoother(&s->smooth_drive, base_drive);
    float atk_s    = process_smoother(&s->smooth_attack, base_atk_ms);
    float rel_s    = process_smoother(&s->smooth_release, base_rel_ms);

    float disp_center = center_s;
    float disp_width = width_s;
    float disp_tilt = tilt_s;
    float disp_drive = drive_s;
    float disp_atk = atk_s;
    float disp_rel = rel_s;
    int   disp_eo = base_eo;

    for (unsigned int i=0;i<frames;i++) {
        float center = center_s;
        float width  = width_s;
        float tilt   = tilt_s;
        float drive  = drive_s;
        float atk_ms = atk_s;
        float rel_ms = rel_s;
        int eo       = base_eo;

        float g_target[BARK_PROC_BANDS];
        for (int b=0;b<BARK_PROC_BANDS;b++) g_target[b] = base_band[b];

        for (int j=0; j<m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;
            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param,"center")==0) center += control;
            else if (strcmp(param,"width")==0) width += control;
            else if (strcmp(param,"tilt")==0) tilt += control;
            else if (strcmp(param,"drive")==0) drive += control;
            else if (strcmp(param,"evenodd")==0) eo += (int)lrintf(control);
            else if (strcmp(param,"attack")==0 || strcmp(param,"atk")==0) atk_ms += control * 50.0f;
            else if (strcmp(param,"release")==0 || strcmp(param,"rel")==0) rel_ms += control * 200.0f;
            else {
                int idx = parse_band_gain_param(param);
                if (idx >= 0) g_target[idx] += control;
            }
        }

        clampf(&center, 0.0f, 1.0f);
        clampf(&width, 0.02f, 1.0f);
        clampf(&tilt, -1.0f, 1.0f);
        clampf(&drive, 0.0f, 1.0f);
        clampf(&atk_ms, 0.1f, 200.0f);
        clampf(&rel_ms, 1.0f, 1000.0f);
        if (eo < 0) eo = 0;
        if (eo > 2) eo = 2;

        disp_center = center;
        disp_width = width;
        disp_tilt = tilt;
        disp_drive = drive;
        disp_atk = atk_ms;
        disp_rel = rel_ms;
        disp_eo = eo;

        float xm = mod_in[i];
        float xc = car_in[i];
        if (!isfinite(xm)) xm = 0.0f;
        if (!isfinite(xc)) xc = 0.0f;

        const float CAR_GAIN = 1.0f;
        xc *= CAR_GAIN;

        float atk_c = coeff_from_ms(atk_ms, s->sample_rate);
        float rel_c = coeff_from_ms(rel_ms, s->sample_rate);

        float sum  = 0.0f;

        for (int b=0;b<BARK_PROC_BANDS;b++) {
            if (eo==1 && (b&1)) continue;
            if (eo==2 && !(b&1)) continue;

			float ym = (fabsf(xm) < 1e-4f) ? 0.0f : xm;
            float yc = xc;

            for (int st=0; st<BARK_PROC_STAGES; st++) {
                ym = biquad_tick_state(s, b, st, ym, &s->z1_mod[b][st], &s->z2_mod[b][st]);
                yc = biquad_tick_state(s, b, st, yc, &s->z1_car[b][st], &s->z2_car[b][st]);
            }
			yc *= s->band_norm[b];

            float rect = fabsf(ym);
			if (rect < 1e-4f) rect = 0.0f;
            float e = s->env[b];
            float c = (rect > e) ? atk_c : rel_c;
            e += c * (rect - e);
			if (e < 1e-8f) e = 0.0f;
            s->env[b] = e;

            float g = g_target[b];
            clampf(&g, 0.0f, 2.0f);
            float g_s = process_smoother(&s->smooth_band[b], g);

            float w = center_window(b, center, width);
            float t = tilt_gain(b, tilt);

			float wt = (g_s * w * t);
			float band = yc * e * wt;
			band = band / (1.0f + fabsf(band));
			sum += band;
        }

		sum *= 0.55f; // Attenuation of final output before sat
        float y = soft_sat(sum, drive);
        out[i] = fminf(fmaxf(y, -1.0f), 1.0f);
    }

    pthread_mutex_lock(&s->lock);
    s->display_center = disp_center;
    s->display_width = disp_width;
    s->display_tilt = disp_tilt;
    s->display_drive = disp_drive;
    s->display_attack_ms = disp_atk;
    s->display_release_ms = disp_rel;
    s->display_even_odd = disp_eo;
    s->display_sel_gain = s->band_gain[s->sel_band];
    pthread_mutex_unlock(&s->lock);
}

static void bark_processor_draw_ui(Module* m, int y, int x) {
    BarkProcessor* s = (BarkProcessor*)m->state;

    float center,width,tilt,drive,atk,rel;
    int eo; int sb; float sg;
    char cmd[64] = "";

    pthread_mutex_lock(&s->lock);
    center = s->display_center;
    width = s->display_width;
    tilt = s->display_tilt;
    drive = s->display_drive;
    atk = s->display_attack_ms;
    rel = s->display_release_ms;
    eo = s->display_even_odd;
    sb = s->sel_band;
    sg = s->display_sel_gain;
    if (s->entering_command) snprintf(cmd, sizeof(cmd), ":%s", s->command_buffer);
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[BarkVocoder:%s] ", m->name);
    CLR();

    LABEL(2,"ctr:"); ORANGE(); printw("%.2f", center); CLR();
    LABEL(2,"wid:"); ORANGE(); printw("%.2f", width); CLR();
    LABEL(2,"tilt:"); ORANGE(); printw("%.2f", tilt); CLR();
    LABEL(2,"drv:"); ORANGE(); printw("%.2f", drive); CLR();
    LABEL(2,"atk:"); ORANGE(); printw("%4.1fms", atk); CLR();
    LABEL(2,"rel:"); ORANGE(); printw("%5.1fms", rel); CLR();
    LABEL(2,"eo:"); ORANGE(); printw("%d", eo); CLR();
    LABEL(2,"b:"); ORANGE(); printw("%02d", sb); CLR();
    LABEL(2,"g:"); ORANGE(); printw("%.2f", sg); CLR();

    YELLOW();
    mvprintw(y+1, x, "-/= ctr _/+ wid  [/] tilt  {/} drv  a/A atk  r/R rel  e eo  b/B band  ;/' gain");
    mvprintw(y+2, x, ":1[ctr] :2[wid] :3[tilt] :4[drv] :5[eo] :6[band] :7[gain] :8[atk_ms] :9[rel_ms]");
    BLACK();
}

static void bark_processor_handle_input(Module* m, int key) {
    BarkProcessor* s = (BarkProcessor*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);
    if (!s->entering_command) {
        switch (key) {
            case '=': s->center += 0.01f; handled=1; break;
            case '-': s->center -= 0.01f; handled=1; break;
            case '+': s->width += 0.01f; handled=1; break;
            case '_': s->width -= 0.01f; handled=1; break;
            case ']': s->tilt += 0.01f; handled=1; break;
            case '[': s->tilt -= 0.01f; handled=1; break;
            case '}': s->drive += 0.01f; handled=1; break;
            case '{': s->drive -= 0.01f; handled=1; break;

            case 'A': s->attack_ms += 1.0f; handled=1; break;
            case 'a': s->attack_ms -= 1.0f; handled=1; break;
            case 'R': s->release_ms += 5.0f; handled=1; break;
            case 'r': s->release_ms -= 5.0f; handled=1; break;

            case 'e': s->even_odd = (s->even_odd + 1) % 3; handled=1; break;

            case 'B': s->sel_band += 1; handled=1; break;
            case 'b': s->sel_band -= 1; handled=1; break;
            case '\'': s->band_gain[s->sel_band] += 0.01f; handled=1; break;
            case ';': s->band_gain[s->sel_band] -= 0.01f; handled=1; break;

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
                if (type=='1') s->center = val;
                else if (type=='2') s->width = val;
                else if (type=='3') s->tilt = val;
                else if (type=='4') s->drive = val;
                else if (type=='5') s->even_odd = (int)val;
                else if (type=='6') s->sel_band = (int)val;
                else if (type=='7') s->band_gain[s->sel_band] = val;
                else if (type=='8') s->attack_ms = val;
                else if (type=='9') s->release_ms = val;
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

    if (handled) clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void bark_processor_set_osc_param(Module* m, const char* param, float value) {
    BarkProcessor* s = (BarkProcessor*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(param,"center")==0) s->center = value;
    else if (strcmp(param,"width")==0) s->width = value;
    else if (strcmp(param,"tilt")==0) s->tilt = value;
    else if (strcmp(param,"drive")==0) s->drive = value;
    else if (strcmp(param,"attack")==0 || strcmp(param,"atk")==0) s->attack_ms = value;
    else if (strcmp(param,"release")==0 || strcmp(param,"rel")==0) s->release_ms = value;
    else if (strcmp(param,"evenodd")==0) s->even_odd = (int)value;
    else {
        int idx = parse_band_gain_param(param);
        if (idx >= 0) s->band_gain[idx] = value;
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void bark_processor_destroy(Module* m) {
    BarkProcessor* s = (BarkProcessor*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    BarkProcessor* s = calloc(1, sizeof(BarkProcessor));
    s->sample_rate = sample_rate;

    /* vocoder defaults */
    s->center = 0.5f;
    s->width = 0.5f;
    s->tilt = 0.0f;
    s->drive = 0.1f;
    s->attack_ms = 0.4f;
    s->release_ms = 8.0f;

    s->even_odd = 0;
    s->sel_band = 0;

    for (int i=0;i<BARK_PROC_BANDS;i++) s->band_gain[i] = 1.0f;

    if (args && strstr(args,"center="))  sscanf(strstr(args,"center="),  "center=%f", &s->center);
    if (args && strstr(args,"width="))   sscanf(strstr(args,"width="),   "width=%f", &s->width);
    if (args && strstr(args,"tilt="))    sscanf(strstr(args,"tilt="),    "tilt=%f", &s->tilt);
    if (args && strstr(args,"drive="))   sscanf(strstr(args,"drive="),   "drive=%f", &s->drive);
    if (args && strstr(args,"attack="))  sscanf(strstr(args,"attack="),  "attack=%f", &s->attack_ms);
    if (args && strstr(args,"release=")) sscanf(strstr(args,"release="), "release=%f", &s->release_ms);
    if (args && strstr(args,"evenodd=")) sscanf(strstr(args,"evenodd="), "evenodd=%d", &s->even_odd);

    pthread_mutex_init(&s->lock, NULL);

    init_smoother(&s->smooth_center,  0.75f);
    init_smoother(&s->smooth_width,   0.75f);
    init_smoother(&s->smooth_tilt,    0.50f);
    init_smoother(&s->smooth_drive,   0.50f);
    init_smoother(&s->smooth_attack,  0.60f);
    init_smoother(&s->smooth_release, 0.60f);
    for (int i=0;i<BARK_PROC_BANDS;i++) init_smoother(&s->smooth_band[i], 0.50f);

    clamp_params(s);
    rebuild_filters(s);

    s->display_center = s->center;
    s->display_width = s->width;
    s->display_tilt = s->tilt;
    s->display_drive = s->drive;
    s->display_attack_ms = s->attack_ms;
    s->display_release_ms = s->release_ms;
    s->display_even_odd = s->even_odd;
    s->display_sel_gain = s->band_gain[s->sel_band];

    Module* m = calloc(1, sizeof(Module));
    m->name = "bark_processor";
    m->state = s;

    /* expects 2 audio inputs (modulator, carrier) */
    m->num_inputs = 2;

    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = bark_processor_process;
    m->draw_ui = bark_processor_draw_ui;
    m->handle_input = bark_processor_handle_input;
    m->set_param = bark_processor_set_osc_param;
    m->destroy = bark_processor_destroy;
    return m;
}

