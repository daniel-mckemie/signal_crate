#ifndef SPEC_RINGMOD_H
#define SPEC_RINGMOD_H

#include <stdbool.h>
#include <pthread.h>
#include <fftw3.h>
#include "module.h"
#include "util.h"

#define SPEC_RINGMOD_FFT_SIZE 4096
#define SPEC_RINGMOD_HOP_SIZE (SPEC_RINGMOD_FFT_SIZE / 4)

#define SPEC_OP_COUNT 6

typedef enum {
	SPEC_OP_RING,          // X * Y
	SPEC_OP_AMP_ONLY,      // |Y| * X
	SPEC_OP_CROSS_SYNTH,   // |Y| + phase(X)
	SPEC_OP_SPECTRAL_AM,   // X * (1 + α|Y|)
	SPEC_OP_SUBTRACT,      // X - Y
	SPEC_OP_MIN_MAG,       // min(|X|,|Y|) + phase(X)
} SpecRingOp;

typedef struct {
    float mix;
    float car_amp;
    float mod_amp;
    float bandlimit_low;
    float bandlimit_high;
	SpecRingOp op;

	float* td_car_win;
	float* td_mod_win;
	float* y_mag_smooth;
	float* y_mag_hold;

    float sample_rate;

    float display_mix;
    float display_car_amp;
    float display_mod_amp;
    float display_bandlimit_low;
    float display_bandlimit_high;

    CParamSmooth smooth_mix;
    CParamSmooth smooth_car_amp;
    CParamSmooth smooth_mod_amp;
    CParamSmooth smooth_bandlimit_low;
    CParamSmooth smooth_bandlimit_high;

    pthread_mutex_t lock;

    float* td_car;
    float* td_mod;
    float* td_out;

    fftwf_complex* X;
    fftwf_complex* Y;
    fftwf_complex* Z;

    fftwf_complex* FX;
    fftwf_complex* FY;

    fftwf_plan plan_car_fwd;
    fftwf_plan plan_mod_fwd;
    fftwf_plan plan_inv;
    fftwf_plan plan_conv_x;
    fftwf_plan plan_conv_y;
	fftwf_plan plan_conv_inv;

    float* window;
    float* ola_buffer;
    float ola_norm;
    int write_pos;
	int hop_pos;

    int bin_low;
    int bin_high;

    /* ---- Command mode ---- */
    bool entering_command;
    char command_buffer[64];
    int  command_index;
} SpecRingMod;

#endif
