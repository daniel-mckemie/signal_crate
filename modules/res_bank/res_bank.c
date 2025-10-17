#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "res_bank.h"
#include "module.h"
#include "util.h"

static void clamp_params(ResBank* s) {
    if (s->lo_hz < 20.0f) s->lo_hz = 20.0f;
    float ny = s->sample_rate * 0.45f;
    if (s->hi_hz > ny) s->hi_hz = ny;
    if (s->hi_hz < s->lo_hz + 1.0f) s->hi_hz = s->lo_hz + 1.0f;

    clampf(&s->mix,   0.0f, 1.0f);
    clampf(&s->drive, 0.0f, 1.0f);
    clampf(&s->regen, 0.0f, 1.0f);
    clampf(&s->q,     0.3f, 40.0f);
    clampf(&s->tilt, -1.0f, 1.0f);
    clampf(&s->odd,  -1.0f, 1.0f);

    if (s->bands < 1) s->bands = 1;
    if (s->bands > RES_MAX_BANDS) s->bands = RES_MAX_BANDS;
}

static void rebuild_centers(ResBank* s) {
    int N = s->bands;
    double lo = s->display_lo_hz;
    double hi = s->display_hi_hz;
    if (lo < 20.0) lo = 20.0;
    if (hi > s->sample_rate * 0.45) hi = s->sample_rate * 0.45;
    for (int i=0;i<N;i++) {
        double t = (N==1) ? 0.5 : (double)i/(double)(N-1);
        s->f[i] = (float)(lo * pow(hi/lo, t)); // log-spaced
    }
    s->need_centers = 0;
    s->need_coeffs = 1;
}

static void rebuild_weights(ResBank* s) {
    int N = s->bands;
    float tilt = s->display_tilt; // -1..+1
    float odd  = s->display_odd;  // -1..+1
    for (int i=0;i<N;i++) {
        float t = (N==1) ? 0.5f : (float)i/(float)(N-1);
		float tilt_pow = powf(2.0f, tilt * (t-0.5f) * 4.0f);
        float w_tilt = tilt_pow; 
		// float w_tilt = (1.f - tilt) + 2.f*tilt * t;       // low↔high emphasis
        float w_odd  = (i & 1) ? (1.f + odd) : (1.f - odd); // odd vs even
        s->w[i] = w_tilt * w_odd;
    }
}

static void rebuild_coeffs(ResBank* s) {
    int N = s->bands;
    float Q = s->display_q;
    if (Q < 0.3f) Q = 0.3f;
    for (int i=0;i<N;i++) {
        float omega = (float)(2.0 * M_PI * s->f[i] / s->sample_rate);
        float sn = sinf(omega), cs = cosf(omega);
        float alpha = sn / (2.0f * Q);
        float b0 =   alpha;
        float b1 =   0.0f;
        float b2 =   -alpha;
        float a0 =   1.0f + alpha;
        float a1 =  -2.0f * cs;
        float a2 =   1.0f - alpha;
        s->b0[i] = b0/a0; s->b1[i] = b1/a0; s->b2[i] = b2/a0;
        s->a1[i] = a1/a0; s->a2[i] = a2/a0;
    }
    s->need_coeffs = 0;
}

static inline float biquad_tick(ResBank* s, int i, float x) {
    float y = s->b0[i]*x + s->z1[i];
    s->z1[i] = s->b1[i]*x + s->z2[i] - s->a1[i]*y;
    s->z2[i] = s->b2[i]*x - s->a2[i]*y;
    return y;
}

static inline float soft_sat(float x, float drive) {
    // 0..1 → gentle → stronger
    float g = 1.0f + 9.0f * drive;
    float y = g * x;
    return y / (1.0f + fabsf(y)); // fast soft clip
}

static void res_bank_process(Module* m, float* in, unsigned long frames) {
    ResBank* s = (ResBank*)m->state;
    float mix, q, lo, hi, tilt, odd, drive, regen;
    int bands;
	
    pthread_mutex_lock(&s->lock);
    mix   = process_smoother(&s->smooth_mix,   s->mix);
    q     = process_smoother(&s->smooth_q,     s->q);
    lo    = process_smoother(&s->smooth_lo_hz,    s->lo_hz);
    hi    = process_smoother(&s->smooth_hi_hz,    s->hi_hz);
    tilt  = process_smoother(&s->smooth_tilt,  s->tilt);
    odd   = process_smoother(&s->smooth_odd,   s->odd);
    drive = process_smoother(&s->smooth_drive, s->drive);
    regen = process_smoother(&s->smooth_regen, s->regen);
    bands = s->bands;
	if (fabsf(q - s->display_q) > 0.01f) s->need_coeffs = 1;
    pthread_mutex_unlock(&s->lock);

	float mod_depth = 1.0f;
	for (int i=0; i<m->num_control_inputs; i++) {
		if (!m->control_inputs[i] || !m->control_input_params[i]) continue;

		const char* param= m->control_input_params[i];
		float control = *(m->control_inputs[i]);
		float norm = fminf(fmaxf(control, -1.0f), 1.0f);

		if (strcmp(param, "mix") == 0) {
			float mod_range = (1.0f - s->mix) * mod_depth;
			mix = s->mix + norm * mod_range;
		} else if (strcmp(param, "q") == 0) {
			float mod_range = (1.0f - s->q) * mod_depth;
			q = s->q + norm * mod_range;
		} else if (strcmp(param, "lo") == 0) {
			float mod_range = s->lo_hz * mod_depth;
			lo = s->lo_hz + norm * mod_range;
		} else if (strcmp(param, "hi") == 0) {
			float mod_range = s->hi_hz * mod_depth;
			hi = s->hi_hz + norm * mod_range;
		} else if (strcmp(param, "tilt") == 0) {
			float mod_range = (2.0f - fabsf(s->tilt)) * mod_depth;
			tilt = s->tilt + norm * mod_range;
		} else if (strcmp(param, "odd") == 0) {
			float mod_range = (2.0f - fabsf(s->odd)) * mod_depth;
			odd = s->odd + norm * mod_range;
		} else if (strcmp(param, "drive") == 0) {
			float mod_range = (1.0f - s->drive) * mod_depth;
			drive = s->drive + norm * mod_range;
		} else if (strcmp(param, "regen") == 0) {
			float mod_range = (1.0f - s->regen) * mod_depth;
			regen = s->regen + norm * mod_range;
		} else if (strcmp(param, "bands") == 0) {
			float mod_range = (RES_MAX_BANDS - s->bands) * mod_depth;
			bands = s->bands + norm * mod_range;
		}
	}

    // cache for UI
    s->display_mix = mix;
	s->display_q=q;
	s->display_lo_hz=lo;
	s->display_hi_hz=hi;
    s->display_tilt=tilt; 
	s->display_odd=odd;
	s->display_drive=drive;
	s->display_regen=regen;
	s->display_bands=bands;

    // Rebuild structures if needed (block edge)
    if (s->need_centers) rebuild_centers(s);
    rebuild_weights(s);
    if (s->need_coeffs) rebuild_coeffs(s);

    // Process
    for (unsigned long n=0; n<frames; ++n) {
        float x = in[n] + 1e-20f; // denorm guard
        float sum = 0.0f;

        // parallel BPFs with tiny regeneration
        float x_fb = x;
        for (int i=0;i<bands;i++) {
            float y = biquad_tick(s, i, x_fb);
            sum += s->w[i] * y;
			float fb = tanhf(y);
            x_fb += regen * (0.008f / (float)bands) * fb; 
			// x_fb += regen * 0.02f * y; // subtle "ring"
        }

        float wet = soft_sat(sum, drive);
        float out = mix*wet + (1.0f - mix)*in[n];
        // final clamp
        if (!isfinite(out)) out = 0.0f;
        if (out > 1.0f) out = 1.0f;
        if (out < -1.0f) out = -1.0f;
        m->output_buffer[n] = out;
    }
}

static void res_bank_draw_ui(Module* m, int y, int x) {
    ResBank* s = (ResBank*)m->state;
    float mix,q,lo,hi,tilt,odd,drive,regen; int bands;
    char cmd[64] = "";

    pthread_mutex_lock(&s->lock);
    mix=s->display_mix;
	q=s->display_q;
	lo=s->display_lo_hz;
	hi=s->display_hi_hz;
	tilt=s->display_tilt;
	odd=s->display_odd;
    drive=s->display_drive;
	regen=s->display_regen;
	bands=s->display_bands;
    if (s->entering_command) snprintf(cmd, sizeof(cmd), ":%s", s->command_buffer);
    pthread_mutex_unlock(&s->lock);

    mvprintw(y,   x, "[ResBank:%s] mix:%.2f q:%.1f lo:%.0f hi:%.0f bands:%d tilt:%.2f odd:%.2f drv:%.2f rgn:%.2f",
             m->name, mix, q, lo, hi, bands, tilt, odd, drive, regen);
    mvprintw(y+1, x, "Real-time: -/= mix, _/+ q, [/] lo, {/} hi, ;/\' bands, ?/\" tilt, ,/. odd, </> drive, 9/0 regen");
    mvprintw(y+2, x, "Cmd mode :1 [mix] :2 [q] :3 [lo] :4 [hi] :5 [bands] :6 [tilt] :7 [odd] :8 [drive] :9 [rgn]");
    if (cmd[0]) mvprintw(y+3, x, "%s", cmd);
}

static void res_bank_handle_input(Module* m, int key) {
    ResBank* s = (ResBank*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);
    if (!s->entering_command) {
        switch (key) {
            case '=': s->mix   += 0.01f; handled = 1; break;
            case '-': s->mix   -= 0.01f; handled = 1; break;
            case '+': s->q     += 0.1f; handled = 1; break;
            case '_': s->q     -= 0.1f; handled = 1; break;
            case ']': s->lo_hz    += 1.0f; handled = 1; break;
            case '[': s->lo_hz    -= 1.0f; handled = 1; break;
            case '}': s->hi_hz    += 1.0f; handled = 1; break;
            case '{': s->hi_hz    -= 1.0f; handled = 1; break;
            case '\'': s->bands += 1;     handled = 1; break;
            case ';': s->bands -= 1;     handled = 1; break;
            case '\"': s->tilt += 0.01f;     handled = 1; break;
			case '?': s->tilt  -= 0.01f;     handled = 1; break;
            case '.': s->odd  += 0.01f; handled = 1; break;
            case ',':  s->odd  -= 0.01f; handled = 1; break;
            case '>': s->drive += 0.01f; handled = 1; break;
            case '<': s->drive -= 0.01f; handled = 1; break;
            case '0': s->regen += 0.01f;     handled = 1; break;
            case '9': s->regen -= 0.01f;     handled = 1; break;
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
                if      (type == '1') s->mix   = val;
                else if (type == '2') s->q     = val;
                else if (type == '3') s->lo_hz = val, s->need_centers=1;
                else if (type == '4') s->hi_hz = val, s->need_centers=1;
                else if (type == '5') s->bands = (int)val, s->need_centers=1;
                else if (type == '6') s->tilt  = val;
                else if (type == '7') s->odd   = val;
                else if (type == '8') s->drive = val;
                else if (type == '9') s->regen = val;
            }
            handled = 1;
        } else if (key == 27) { // ESC
            s->entering_command = false; handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
            s->command_index--;
            s->command_buffer[s->command_index] = '\0'; handled = 1;
        } else if (key >= 32 && key < 127 && s->command_index < (int)sizeof(s->command_buffer) - 1) {
            s->command_buffer[s->command_index++] = (char)key;
            s->command_buffer[s->command_index] = '\0'; handled = 1;
        }
    }

    if (handled) {
        clamp_params(s);
        if (key=='3' || key=='4') s->need_centers = 1;
        // centers affect coeffs too
        if (key=='2') s->need_coeffs = 1;
        // any change in bands/lo/hi also needs centers/coeffs
        if (key=='9' || key=='0') s->need_centers = 1;
    }
    pthread_mutex_unlock(&s->lock);
}

static void res_bank_set_osc_param(Module* m, const char* param, float value) {
    ResBank* s = (ResBank*)m->state;
    pthread_mutex_lock(&s->lock);

    if      (strcmp(param,"mix")==0)   s->mix = fminf(fmaxf(value,0.f),1.f);
    else if (strcmp(param,"q")==0)     s->q   = value, s->need_coeffs=1;
    else if (strcmp(param,"tilt")==0)  s->tilt= fminf(fmaxf(value,-1.f),1.f);
    else if (strcmp(param,"odd")==0)   s->odd = fminf(fmaxf(value,-1.f),1.f);
    else if (strcmp(param,"drive")==0) s->drive = fminf(fmaxf(value,0.f),1.f);
    else if (strcmp(param,"regen")==0) s->regen = fminf(fmaxf(value,0.f),0.5f);
    else if (strcmp(param,"bands")==0) { s->bands = (int)(value + 0.5f); s->need_centers=1; }
    else if (strcmp(param,"lo")==0) {
        // expect 0..1 → 20..20000Hz (exp)
        float min_hz=20.f, max_hz=20000.f; float norm=fminf(fmaxf(value,0.f),1.f);
        s->lo_hz = min_hz * powf(max_hz/min_hz, norm); s->need_centers=1;
    } else if (strcmp(param,"hi")==0) {
        float min_hz=20.f, max_hz=20000.f; float norm=fminf(fmaxf(value,0.f),1.f);
        s->hi_hz = min_hz * powf(max_hz/min_hz, norm); s->need_centers=1;
    } else {
        fprintf(stderr,"[res_bank] Unknown OSC param: %s\n", param);
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void res_bank_destroy(Module* m) {
    ResBank* s = (ResBank*)m->state;
    if (s) pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    // defaults
    float mix=0.5f, q=12.f, lo=120.f, hi=6000.f, tilt=0.f, odd=0.f, drive=0.2f, regen=0.1f;
    int bands=12;

    if (args && strstr(args,"mix="))   sscanf(strstr(args,"mix="),   "mix=%f", &mix);
    if (args && strstr(args,"q="))     sscanf(strstr(args,"q="),     "q=%f", &q);
    if (args && strstr(args,"lo="))    sscanf(strstr(args,"lo="),    "lo=%f", &lo);
    if (args && strstr(args,"hi="))    sscanf(strstr(args,"hi="),    "hi=%f", &hi);
    if (args && strstr(args,"tilt="))  sscanf(strstr(args,"tilt="),  "tilt=%f", &tilt);
    if (args && strstr(args,"odd="))   sscanf(strstr(args,"odd="),   "odd=%f", &odd);
    if (args && strstr(args,"drive=")) sscanf(strstr(args,"drive="), "drive=%f", &drive);
    if (args && strstr(args,"regen=")) sscanf(strstr(args,"regen="), "regen=%f", &regen);
    if (args && strstr(args,"bands=")) sscanf(strstr(args,"bands="), "bands=%d", &bands);

    ResBank* s = calloc(1, sizeof(ResBank));
    s->sample_rate = sample_rate;
    s->mix = mix; s->q=q; s->lo_hz=lo; s->hi_hz=hi; s->tilt=tilt; s->odd=odd; s->drive=drive; s->regen=regen; s->bands=bands;
    pthread_mutex_init(&s->lock, NULL);

    init_smoother(&s->smooth_mix,   0.50f);
    init_smoother(&s->smooth_q,     0.75f);
    init_smoother(&s->smooth_lo_hz,    0.75f);
    init_smoother(&s->smooth_hi_hz,    0.75f);
    init_smoother(&s->smooth_tilt,  0.50f);
    init_smoother(&s->smooth_odd,   0.50f);
    init_smoother(&s->smooth_drive, 0.50f);
    init_smoother(&s->smooth_regen, 0.75f);

    clamp_params(s);
    s->need_centers = 1; s->need_coeffs = 1;

    Module* m = calloc(1, sizeof(Module));
    m->name = "res_bank";
    m->state = s;
    m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));
    m->process = res_bank_process;
    m->draw_ui = res_bank_draw_ui;
    m->handle_input = res_bank_handle_input;
    m->set_param = res_bank_set_osc_param;
    m->destroy = res_bank_destroy;
    return m;
}

