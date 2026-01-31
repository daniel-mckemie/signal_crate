#ifndef BARK_PROCESSOR_H
#define BARK_PROCESSOR_H

#include <pthread.h>
#include <stdbool.h>
#include "util.h"

#define BARK_PROC_BANDS   24
#define BARK_PROC_STAGES  3

typedef struct {
    float sample_rate;

    /* global params */
    float center;     /* 0..1 */
    float width;      /* 0.02..1 */
    float tilt;       /* -1..1 */
    float drive;      /* 0..1 */

    /* Verbos-style input gains for the two banks */
    float out_gain_odd;   /* input A -> odd bands */
    float out_gain_even;  /* input B -> even bands */

    /* cross-mod switches */
    int odd_to_even;   /* odd envelopes modulate even audio */
    int even_to_odd;   /* even envelopes modulate odd audio */

    /* per-band gain */
    float band_gain[BARK_PROC_BANDS];

    /* display */
    float display_center;
    float display_width;
    float display_tilt;
    float display_drive;
    float display_out_gain_odd;
    float display_out_gain_even;
    int   display_odd_to_even;
    int   display_even_to_odd;

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

    /* filter state per band */
    float z1[BARK_PROC_BANDS][BARK_PROC_STAGES];
    float z2[BARK_PROC_BANDS][BARK_PROC_STAGES];

    /* envelope per band (post-filter magnitude follower) */
    float env[BARK_PROC_BANDS];

    /* bark windowing */
    float bark_pos[BARK_PROC_BANDS];
    float bark_min, bark_max;

    /* smoothers */
    CParamSmooth smooth_center;
    CParamSmooth smooth_width;
    CParamSmooth smooth_tilt;
    CParamSmooth smooth_drive;
    CParamSmooth smooth_out_gain_odd;
    CParamSmooth smooth_out_gain_even;
    CParamSmooth smooth_band[BARK_PROC_BANDS];

    /* UI command mode */
    bool entering_command;
    char command_buffer[64];
    int  command_index;

    pthread_mutex_t lock;
} BarkProcessor;

#endif

