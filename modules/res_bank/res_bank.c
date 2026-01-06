#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>

#include "res_bank.h"
#include "module.h"
#include "util.h"

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
	float* input = (m->num_inputs > 0) ? m->inputs[0] : in;
	float* out   = m->output_buffer;

    pthread_mutex_lock(&s->lock);
    float base_mix   = s->mix;
    float base_q     = s->q;
    float base_lo    = s->lo_hz;
    float base_hi    = s->hi_hz;
    float base_tilt  = s->tilt;
    float base_odd   = s->odd;
    float base_drive = s->drive;
    float base_regen = s->regen;
    int base_bands = s->bands;
    int need_coeffs  = s->need_coeffs;
    int need_centers = s->need_centers;
	float sample_rate = s->sample_rate;
    pthread_mutex_unlock(&s->lock);

    float mix_s   = process_smoother(&s->smooth_mix,   base_mix);
    float q_s     = process_smoother(&s->smooth_q,     base_q);
    float lo_s    = process_smoother(&s->smooth_lo_hz, base_lo);
    float hi_s    = process_smoother(&s->smooth_hi_hz, base_hi);
    float tilt_s  = process_smoother(&s->smooth_tilt,  base_tilt);
    float odd_s   = process_smoother(&s->smooth_odd,   base_odd);
    float drive_s = process_smoother(&s->smooth_drive, base_drive);
    float regen_s = process_smoother(&s->smooth_regen, base_regen);
    int   bands_s = base_bands;

	int need_centers_block = need_centers;
	int need_coeffs_block  = need_coeffs;

	float disp_mix   = mix_s;
	float disp_q     = q_s;
	float disp_lo    = lo_s;
	float disp_hi    = hi_s;
	float disp_tilt  = tilt_s;
	float disp_odd   = odd_s;
	float disp_drive = drive_s;
	float disp_regen = regen_s;
	int disp_bands   = bands_s;


    for (unsigned int i=0; i<frames; i++) {
		float mix   = mix_s;
		float q     = q_s;
		float lo    = lo_s;
		float hi    = hi_s;
		float tilt  = tilt_s;
		float odd   = odd_s;
		float drive = drive_s;
		float regen = regen_s;
		int bands   = bands_s;


		for (int j=0; j < m->num_control_inputs; j++) {
			if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

			const char* param = m->control_input_params[j];
			float control = m->control_inputs[j][i];
			control = fminf(fmaxf(control, -1.0f), 1.0f);

			if (strcmp(param, "mix") == 0) {
				mix += control;
			}
			else if (strcmp(param, "q") == 0) {
				q += control * 40.0f;
				need_coeffs_block = 1;
			}
			else if (strcmp(param, "lo") == 0) {
				lo += control * base_lo;
				need_centers_block = 1;
			}
			else if (strcmp(param, "hi") == 0) {
				hi += control * base_hi;
				need_centers_block = 1;
			}
			else if (strcmp(param, "tilt") == 0) {
				tilt += control;
			}
			else if (strcmp(param, "odd") == 0) {
				odd += control;
			}
			else if (strcmp(param, "drive") == 0) {
				drive += control;
			}
			else if (strcmp(param, "regen") == 0) {
				regen += control;
			}
			else if (strcmp(param, "bands") == 0) {
				bands += (int) lrintf(control);
				need_centers_block = 1;
			}
		}

		float ny = sample_rate * 0.45f;
		if (lo < 20.0f) lo = 20.0f;
		if (hi > ny) hi = ny;
		if (hi < lo + 1.0f) hi = lo + 1.0f;

		clampf(&mix,   0.0f, 1.0f);
		clampf(&drive, 0.0f, 1.0f);
		clampf(&regen, 0.0f, 1.0f);
		clampf(&q,     0.3f, 40.0f);
		clampf(&tilt, -1.0f, 1.0f);
		clampf(&odd,  -1.0f, 1.0f);

		if (bands < 1) bands = 1;
		if (bands > RES_MAX_BANDS) bands = RES_MAX_BANDS;

		disp_mix   = mix;
		disp_q     = q;
		disp_lo    = lo;
		disp_hi    = hi;
		disp_tilt  = tilt;
		disp_odd   = odd;
		disp_drive = drive;
		disp_regen = regen;
		disp_bands = bands;

		/* DSP */
        float x = (input ? input[i] : 0.0f) + 1e-20f;
        if (!isfinite(x)) x = 0.0f;

        float sum = 0.0f;
        float fb_input = x;

        for (int b=0; b<disp_bands; b++) {
            float y = biquad_tick(s, b, fb_input);
            sum += s->w[b] * y;
            fb_input += disp_regen * (0.008f / (float)disp_bands) * tanhf(y);
        }

        float wet = soft_sat(sum, disp_drive);
        float yout = disp_mix * wet + (1.0f - disp_mix) * x;

        if (!isfinite(yout)) yout = 0.0f;
        yout = fminf(fmaxf(yout, -1.0f), 1.0f);

        out[i] = yout;
    }

	pthread_mutex_lock(&s->lock);
    s->display_mix = disp_mix;
    s->display_q   = disp_q;
    s->display_lo_hz = disp_lo;
    s->display_hi_hz = disp_hi;
    s->display_tilt = disp_tilt;
    s->display_odd  = disp_odd;
    s->display_drive = disp_drive;
    s->display_regen = disp_regen;
    s->display_bands = disp_bands;
	pthread_mutex_unlock(&s->lock);

    if (need_centers_block) {
        rebuild_centers(s); 
        need_coeffs_block = 1;
    }

    rebuild_weights(s);

    if (need_coeffs_block) {
		pthread_mutex_lock(&s->lock);
        s->display_q = disp_q;
		pthread_mutex_unlock(&s->lock);
        rebuild_coeffs(s);
    }
}

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

	BLUE();
    mvprintw(y, x, "[ResBank:%s] ", m->name);
	CLR();
	
	LABEL(2, "mix:");
	ORANGE(); printw("%.2f", mix); CLR();

	LABEL(2, "q:");
	ORANGE(); printw("%.1f", q); CLR();

	LABEL(2, "lo:");
	ORANGE(); printw("%.0f", lo); CLR();
	
	LABEL(2, "hi:");
	ORANGE(); printw("%.0f", hi); CLR();
   
	LABEL(2, "bands:");
	ORANGE(); printw("%d", bands); CLR();

	LABEL(2, "tilt:");
	ORANGE(); printw("%.2f", tilt); CLR();

	LABEL(2, "odd:");
	ORANGE(); printw("%.2f", odd); CLR();

	LABEL(2, "drv:");
	ORANGE(); printw("%.2f", drive); CLR();
	
	LABEL(2, "rgn:");
	ORANGE(); printw("%.2f", regen); CLR();

	YELLOW();
    mvprintw(y+1, x, "-/= mix _/+ q [/] lo {/} hi ;/\' bands ?/\" tilt ,/. odd </> drv 9/0 rgn");
    mvprintw(y+2, x, ":1[mix] :2[q] :3[lo] :4[hi] :5[bnd] :6[tilt] :7[odd] :8[drive] :9[regen]");
	BLACK();
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

    if      (strcmp(param,"mix")==0)   s->mix = fminf(fmaxf(value,0.0f),1.0f);
	else if (strcmp(param,"q")==0) {
		float norm = fminf(fmaxf(value, 0.0f), 1.0f);
		float minQ = 0.3f, maxQ = 40.0f;
		s->q = minQ * powf(maxQ/minQ, norm);   // exponential sweep
		s->need_coeffs = 1;
	}
    else if (strcmp(param,"tilt")==0)  s->tilt= fminf(fmaxf(value,-1.0f),1.0f);
    else if (strcmp(param,"odd")==0)   s->odd = fminf(fmaxf(value,-1.0f),1.0f);
    else if (strcmp(param,"drive")==0) s->drive = fminf(fmaxf(value,0.0f),1.0f);
    else if (strcmp(param,"regen")==0) s->regen = fminf(fmaxf(value,0.0f),1.0f);
	else if (strcmp(param,"bands")==0) {
		float norm = fminf(fmaxf(value, 0.0f), 1.0f);
		float mapped = 1.0f + norm * (RES_MAX_BANDS - 1.0f);
		s->bands = (int)(mapped + 0.5f);
		s->need_centers = 1;
	}
    else if (strcmp(param,"lo")==0) {
        // expect 0..1 → 20..20000Hz (exp)
        float min_hz=20.0f, max_hz=20000.0f; float norm=fminf(fmaxf(value,0.0f),1.0f);
        s->lo_hz = min_hz * powf(max_hz/min_hz, norm); s->need_centers=1;
    } else if (strcmp(param,"hi")==0) {
        float min_hz=20.0f, max_hz=20000.0f; float norm=fminf(fmaxf(value,0.0f),1.0f);
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
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = res_bank_process;
    m->draw_ui = res_bank_draw_ui;
    m->handle_input = res_bank_handle_input;
    m->set_param = res_bank_set_osc_param;
    m->destroy = res_bank_destroy;
    return m;
}

