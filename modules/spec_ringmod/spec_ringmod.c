#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <ncurses.h>
#include <fftw3.h>

#include "spec_ringmod.h"
#include "module.h"
#include "util.h"

static void spec_ringmod_process(Module* m, float* in, unsigned long frames) {
    SpecRingMod* s = (SpecRingMod*)m->state;
    float* in_car = (m->num_inputs > 0) ? m->inputs[0] : in;
    float* in_mod = (m->num_inputs > 1) ? m->inputs[1] : NULL;
    float* out    = m->output_buffer;

    if (!in_car || !in_mod) {
        memset(out, 0, frames * sizeof(float));
        return;
    }

    pthread_mutex_lock(&s->lock);
    float base_mix = s->mix;
    float base_car = s->car_amp;
    float base_mod = s->mod_amp;
    float base_bl  = s->bandlimit_low;
    float base_bh  = s->bandlimit_high;
    float sr       = s->sample_rate;
    pthread_mutex_unlock(&s->lock);

    float mix_s = process_smoother(&s->smooth_mix, base_mix);
    float car_s = process_smoother(&s->smooth_car_amp, base_car);
    float mod_s = process_smoother(&s->smooth_mod_amp, base_mod);

    float disp_mix = mix_s;
    float disp_car = car_s;
    float disp_mod = mod_s;
    float disp_bl  = base_bl;
    float disp_bh  = base_bh;

    const int N    = SPEC_RINGMOD_FFT_SIZE;
    const int H    = SPEC_RINGMOD_HOP_SIZE;
    const int bins = N / 2 + 1;
    const float nyq = sr * 0.5f;

    for (unsigned long i = 0; i < frames; i++) {

        float mix = mix_s;
        float car = car_s;
        float mod = mod_s;
        float bl  = base_bl;
        float bh  = base_bh;

        /* CV inputs */
        for (int j = 0; j < m->num_control_inputs; j++) {
            if (!m->control_inputs[j] || !m->control_input_params[j]) continue;

            const char* param = m->control_input_params[j];
            float control = m->control_inputs[j][i];
            control = fminf(fmaxf(control, -1.0f), 1.0f);

            if (strcmp(param, "mix") == 0)            mix += control;
            else if (strcmp(param, "car_amp") == 0)   car += control;
            else if (strcmp(param, "mod_amp") == 0)   mod += control;
            else if (strcmp(param, "band_low") == 0)  bl  += control * base_bl;
            else if (strcmp(param, "band_high") == 0) bh  += control * base_bh;
        }

        clampf(&mix, 0.0f, 1.0f);
        clampf(&car, 0.0f, 1.0f);
        clampf(&mod, 0.0f, 1.0f);
        clampf(&bl,  20.0f, nyq * 0.9f);
        clampf(&bh,  bl,    nyq * 0.9f);

        disp_mix = mix;
        disp_car = car;
        disp_mod = mod;
        disp_bl  = bl;
        disp_bh  = bh;

        /* write input (raw history) */
        s->td_car[s->write_pos] = in_car[i] * car;
        s->td_mod[s->write_pos] = in_mod[i] * mod;
        s->write_pos++;

        /* output ring (NO per-sample memmove) */
        int ola_r = s->hop_pos; /* reuse hop_pos as OLA read index */
        float y = s->ola_buffer[ola_r];
        out[i] = y * mix + in_car[i] * (1.0f - mix);

        /* consume one sample from the OLA ring */
        s->ola_buffer[ola_r] = 0.0f;
        ola_r++;
        if (ola_r >= N) ola_r = 0;
        s->hop_pos = ola_r;

        if (s->write_pos < N)
            continue;

        /* analysis window */
        for (int n = 0; n < N; n++) {
            s->td_car[n] *= s->window[n];
            s->td_mod[n] *= s->window[n];
        }

        fftwf_execute(s->plan_car_fwd);
        fftwf_execute(s->plan_mod_fwd);

        int bin_low  = (int)((bl / nyq) * (bins - 1));
        int bin_high = (int)((bh / nyq) * (bins - 1));
        clampi(&bin_low,  0, bins - 1);
        clampi(&bin_high, 0, bins - 1);

        /* spectral ring-mod: magnitude × magnitude, carrier phase */
        for (int k = 0; k < bins; k++) {
            if (k < bin_low || k > bin_high) {
                s->Z[k][0] = 0.0f;
                s->Z[k][1] = 0.0f;
                continue;
            }

            float xr = s->X[k][0], xi = s->X[k][1];
            float yr = s->Y[k][0], yi = s->Y[k][1];

            float mag_x = sqrtf(xr * xr + xi * xi);
            float mag_y = sqrtf(yr * yr + yi * yi);

            float phase = atan2f(xi, xr);
            float mag   = mag_x * mag_y;

            s->Z[k][0] = mag * cosf(phase);
            s->Z[k][1] = mag * sinf(phase);
        }

        fftwf_execute(s->plan_inv);

        /* IFFT scaling + synthesis window (sqrt-Hann) */
        const float invN = 1.0f / (float)N;
        for (int n = 0; n < N; n++) {
            s->td_out[n] = s->td_out[n] * invN * s->window[n];
        }

        /* overlap-add into ring, aligned to current read head */
        int w0 = s->hop_pos;
        for (int n = 0; n < N; n++) {
            int idx = w0 + n;
            if (idx >= N) idx -= N;
            s->ola_buffer[idx] += s->td_out[n];
        }

        /* shift input history by hop (still block-rate, not sample-rate) */
        memmove(s->td_car, s->td_car + H, sizeof(float) * (N - H));
        memmove(s->td_mod, s->td_mod + H, sizeof(float) * (N - H));
        memset(s->td_car + (N - H), 0, sizeof(float) * H);
        memset(s->td_mod + (N - H), 0, sizeof(float) * H);

        s->write_pos = (N - H);
	}

    pthread_mutex_lock(&s->lock);
    s->display_mix = disp_mix;
    s->display_car_amp = disp_car;
    s->display_mod_amp = disp_mod;
    s->display_bandlimit_low = disp_bl;
    s->display_bandlimit_high = disp_bh;
    pthread_mutex_unlock(&s->lock);
}

static void clamp_params(SpecRingMod* s) {
    clampf(&s->mix, 0.0f, 1.0f);
    clampf(&s->car_amp, 0.0f, 1.0f);
    clampf(&s->mod_amp, 0.0f, 1.0f);
    clampf(&s->bandlimit_low, 20.0f, s->sample_rate * 0.45f);
    clampf(&s->bandlimit_high,
           s->bandlimit_low,
           s->sample_rate * 0.45f);
}

static void spec_ringmod_draw_ui(Module* m, int y, int x) {
    SpecRingMod* s = (SpecRingMod*)m->state;
    float mix, car, mod, bl, bh;
    char cmd[64] = "";

    pthread_mutex_lock(&s->lock);
    mix = s->display_mix;
    car = s->display_car_amp;
    mod = s->display_mod_amp;
    bl  = s->display_bandlimit_low;
    bh  = s->display_bandlimit_high;
    if (s->entering_command)
        snprintf(cmd, sizeof(cmd), ":%s", s->command_buffer);
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[SpecRM:%s] ", m->name);
    CLR();

    LABEL(2, "b:");
    ORANGE(); printw(" %.1f-%.1f Hz ", bl, bh); CLR();

    LABEL(2, "car:");
    ORANGE(); printw(" %.2f ", car); CLR();

    LABEL(2, "mod:");
    ORANGE(); printw(" %.2f ", mod); CLR();
	
    LABEL(2, "mix:");
    ORANGE(); printw(" %.2f ", mix); CLR();

    YELLOW();
    mvprintw(y+1, x, "Keys: -/= (bl) _/+ (bh) [/] (car) {/} (mod) \'/;");
    mvprintw(y+2, x, "Cmd: :1 band_low :2 band_high :3 car_amp :4 mod_amp :5 mix");
    BLACK();
}

static void spec_ringmod_handle_input(Module* m, int key) {
    SpecRingMod* s = (SpecRingMod*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        switch (key) {
            case '=': s->bandlimit_low += 0.5f; handled = 1; break;
            case '-': s->bandlimit_low -= 0.5f; handled = 1; break;
            case '+': s->bandlimit_high += 1.0f; handled = 1; break;
            case '_': s->bandlimit_high -= 1.0f; handled = 1; break;
            case ']': s->car_amp += 0.01f; handled = 1; break;
            case '[': s->car_amp -= 0.01f; handled = 1; break;
            case '}': s->mod_amp += 0.01f; handled = 1; break;
            case '{': s->mod_amp -= 0.01f; handled = 1; break;
            case '\'': s->mix += 0.01f; handled = 1; break;
            case ';': s->mix -= 0.01f; handled = 1; break;
            case ':':
                s->entering_command = 1;
                memset(s->command_buffer, 0, sizeof(s->command_buffer));
                s->command_index = 0;
                handled = 1;
                break;
        }
    } else {
        if (key == '\n') {
            s->entering_command = 0;
            char type;
            float val;
            if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
                if      (type == '1') s->bandlimit_low = val;
                else if (type == '2') s->bandlimit_high = val;
                else if (type == '3') s->car_amp = val;
                else if (type == '4') s->mod_amp = val;
                else if (type == '5') s->mix = val;
            }
            handled = 1;
        } else if (key == 27) {
            s->entering_command = 0;
            handled = 1;
        } else if ((key == KEY_BACKSPACE || key == 127) &&
                   s->command_index > 0) {
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
}

static void spec_ringmod_set_osc_param(Module* m, const char* p, float v) {
    SpecRingMod* s = (SpecRingMod*)m->state;
    pthread_mutex_lock(&s->lock);

    if (strcmp(p, "mix") == 0) {
        s->mix = fminf(fmaxf(v, 0.0f), 1.0f);
    } else if (strcmp(p, "car_amp") == 0) {
        s->car_amp = fminf(fmaxf(v, 0.0f), 1.0f) * 1.0f;
    } else if (strcmp(p, "mod_amp") == 0) {
        s->mod_amp = fminf(fmaxf(v, 0.0f), 1.0f) * 1.0f;
    } else if (strcmp(p, "band_low") == 0) {
        s->bandlimit_low = v * s->sample_rate * 0.45f;
    } else if (strcmp(p, "band_high") == 0) {
        s->bandlimit_high = v * s->sample_rate * 0.45f;
    } else {
        fprintf(stderr, "[spec_ringmod] Unknown OSC param: %s\n", p);
    }

    clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void spec_ringmod_destroy(Module* m) {
    SpecRingMod* s = (SpecRingMod*)m->state;
    if (!s) return;

    fftwf_destroy_plan(s->plan_car_fwd);
    fftwf_destroy_plan(s->plan_mod_fwd);
    fftwf_destroy_plan(s->plan_inv);
    fftwf_destroy_plan(s->plan_conv_x);
    fftwf_destroy_plan(s->plan_conv_y);
	fftwf_destroy_plan(s->plan_conv_inv);

    fftwf_free(s->X);
    fftwf_free(s->Y);
    fftwf_free(s->Z);
    fftwf_free(s->FX);
    fftwf_free(s->FY);
    fftwf_free(s->frozen_Y);
    fftwf_free(s->td_car);
    fftwf_free(s->td_mod);
    fftwf_free(s->td_out);

    free(s->window);
    free(s->ola_buffer);
    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    SpecRingMod* s = calloc(1, sizeof(SpecRingMod));
    s->mix = 1.0f;
    s->car_amp = 0.5f;
    s->mod_amp = 0.1f;
    s->bandlimit_low = 20.0f;
    s->bandlimit_high = 4000.0f; 
    s->sample_rate = sample_rate;

    pthread_mutex_init(&s->lock, NULL);
    init_smoother(&s->smooth_mix, 0.75f);
    init_smoother(&s->smooth_car_amp, 0.75f);
    init_smoother(&s->smooth_mod_amp, 0.75f);
    clamp_params(s);

    s->td_car = fftwf_alloc_real(SPEC_RINGMOD_FFT_SIZE);
    s->td_mod = fftwf_alloc_real(SPEC_RINGMOD_FFT_SIZE);
    s->td_out = fftwf_alloc_real(SPEC_RINGMOD_FFT_SIZE);
    s->ola_buffer = calloc(SPEC_RINGMOD_FFT_SIZE, sizeof(float));

    s->window = malloc(sizeof(float) * SPEC_RINGMOD_FFT_SIZE);
	for (int i = 0; i < SPEC_RINGMOD_FFT_SIZE; i++) {
		float w = 0.5f * (1.0f - cosf(2.0f * M_PI * i /
				  (SPEC_RINGMOD_FFT_SIZE - 1)));
		s->window[i] = sqrtf(w);
	}


    s->X = fftwf_alloc_complex(SPEC_RINGMOD_FFT_SIZE);
    s->Y = fftwf_alloc_complex(SPEC_RINGMOD_FFT_SIZE);
    s->Z = fftwf_alloc_complex(SPEC_RINGMOD_FFT_SIZE);
    s->FX = fftwf_alloc_complex(SPEC_RINGMOD_FFT_SIZE);
    s->FY = fftwf_alloc_complex(SPEC_RINGMOD_FFT_SIZE);
    s->frozen_Y = fftwf_alloc_complex(SPEC_RINGMOD_FFT_SIZE);

    s->plan_car_fwd =
        fftwf_plan_dft_r2c_1d(SPEC_RINGMOD_FFT_SIZE,
                              s->td_car, s->X, FFTW_ESTIMATE);
    s->plan_mod_fwd =
        fftwf_plan_dft_r2c_1d(SPEC_RINGMOD_FFT_SIZE,
                              s->td_mod, s->Y, FFTW_ESTIMATE);
    s->plan_inv =
        fftwf_plan_dft_c2r_1d(SPEC_RINGMOD_FFT_SIZE,
                              s->Z, s->td_out, FFTW_ESTIMATE);
    s->plan_conv_x =
        fftwf_plan_dft_1d(SPEC_RINGMOD_FFT_SIZE,
                          s->X, s->FX, FFTW_FORWARD, FFTW_ESTIMATE);
    s->plan_conv_y =
        fftwf_plan_dft_1d(SPEC_RINGMOD_FFT_SIZE,
                          s->Y, s->FY, FFTW_FORWARD, FFTW_ESTIMATE);
	s->plan_conv_inv =
		fftwf_plan_dft_1d(SPEC_RINGMOD_FFT_SIZE,
						  s->Z, s->Z, FFTW_BACKWARD, FFTW_ESTIMATE);


    Module* m = calloc(1, sizeof(Module));
    m->name = "spec_ringmod";
    m->state = s;
    m->process = spec_ringmod_process;
    m->draw_ui = spec_ringmod_draw_ui;
    m->handle_input = spec_ringmod_handle_input;
	m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->set_param = spec_ringmod_set_osc_param;
    m->destroy = spec_ringmod_destroy;

    return m;
}

