#ifndef BARK_BANK_H
#define BARK_BANK_H

#include <pthread.h>
#include "util.h"

#define BARK_BANDS 24
#define BARK_STAGES 3

typedef struct {
    float sample_rate;

    float mix;
    float center;
    float width;
    float tilt;
    float drive;
    int   even_odd;                 /* 0=all 1=even 2=odd */

    float band_gain[BARK_BANDS];

    float display_mix;
    float display_center;
    float display_width;
    float display_tilt;
    float display_drive;
    int   display_even_odd;

    int   sel_band;
    float display_sel_gain;

    float fc[BARK_BANDS];
    float Q[BARK_BANDS];

    float b0[BARK_BANDS][BARK_STAGES];
    float b1[BARK_BANDS][BARK_STAGES];
    float b2[BARK_BANDS][BARK_STAGES];
    float a1[BARK_BANDS][BARK_STAGES];
    float a2[BARK_BANDS][BARK_STAGES];

    float z1[BARK_BANDS][BARK_STAGES];
    float z2[BARK_BANDS][BARK_STAGES];

    CParamSmooth smooth_mix;
    CParamSmooth smooth_center;
    CParamSmooth smooth_width;
    CParamSmooth smooth_tilt;
    CParamSmooth smooth_drive;
    CParamSmooth smooth_band[BARK_BANDS];

    bool entering_command;
    char command_buffer[64];
    int  command_index;

    pthread_mutex_t lock;
} BarkBank;

#endif

