#ifndef BARK_PROCESSOR_H
#define BARK_PROCESSOR_H

#include <pthread.h>
#include <stdbool.h>
#include "util.h"

#define BARK_PROC_BANDS  24
#define BARK_PROC_STAGES 2   /* 6th-order = 3 biquads */

typedef struct {
    float sample_rate;

    /* global params */
    float mix;        /* wet = vocoded, dry = carrier */
    float center;
    float width;
    float tilt;
    float drive;
    int   even_odd;   /* 0=all 1=even 2=odd */

    /* vocoder envelope */
    float attack_ms;
    float release_ms;

    /* per-band base gain */
    float band_gain[BARK_PROC_BANDS];
	float band_norm[BARK_PROC_BANDS];

    /* display */
    float display_mix;
    float display_center;
    float display_width;
    float display_tilt;
    float display_drive;
    float display_attack_ms;
    float display_release_ms;
    int   display_even_odd;

    int   sel_band;
    float display_sel_gain;

    /* filter design */
    float fc[BARK_PROC_BANDS];
    float Q[BARK_PROC_BANDS];

    float b0[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float b1[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float b2[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float a1[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float a2[BARK_PROC_BANDS][BARK_PROC_STAGES];

    /* separate filter state:
       modulator vs carrier */
    float z1_mod[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float z2_mod[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float z1_car[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float z2_car[BARK_PROC_BANDS][BARK_PROC_STAGES];

    /* per-band envelopes (from modulator) */
    float env[BARK_PROC_BANDS];

    /* smoothers */
    CParamSmooth smooth_mix;
    CParamSmooth smooth_center;
    CParamSmooth smooth_width;
    CParamSmooth smooth_tilt;
    CParamSmooth smooth_drive;
    CParamSmooth smooth_attack;
    CParamSmooth smooth_release;
    CParamSmooth smooth_band[BARK_PROC_BANDS];

    /* UI command mode */
    bool entering_command;
    char command_buffer[64];
    int  command_index;

    pthread_mutex_t lock;
} BarkProcessor;

#endif

