// vocoder.h
#ifndef VOCODER_H
#define VOCODER_H

#include "util.h"
#include <pthread.h>
#include <stdbool.h>

#define VOCODER_BANDS 24
#define VOCODER_STAGES 3

typedef struct {
  float sample_rate;

  /* gains + mix */
  float mod_gain; /* 0..2 */
  float car_gain; /* 0..2 */
  float mix;      /* 0..1 */
  float drive;    /* 0..1 */
  float out_trim; /* 0..2 */

  /* spectral shaping */
  float tilt;   /* -1..1 */
  float center; /* 0..1 */
  float width;  /* 0.02..1 */

  /* envelope follower */
  float atk_ms;    /* 0.1..200 */
  float rel_ms;    /* 1..1000 */
  float env_curve; /* 0.25..4.0 */

  /* per-band gain */
  float band_gain[VOCODER_BANDS];

  /* display */
  float display_mod_gain;
  float display_car_gain;
  float display_mix;
  float display_drive;
  float display_out_trim;

  float display_tilt;
  float display_center;
  float display_width;

  float display_atk_ms;
  float display_rel_ms;
  float display_env_curve;

  int sel_band;
  float display_sel_gain;

  /* filter design */
  float fc[VOCODER_BANDS];
  float Q[VOCODER_BANDS];

  float b0[VOCODER_BANDS][VOCODER_STAGES];
  float b1[VOCODER_BANDS][VOCODER_STAGES];
  float b2[VOCODER_BANDS][VOCODER_STAGES];
  float a1[VOCODER_BANDS][VOCODER_STAGES];
  float a2[VOCODER_BANDS][VOCODER_STAGES];

  /* filter state (separate mod/car) */
  float z1m[VOCODER_BANDS][VOCODER_STAGES];
  float z2m[VOCODER_BANDS][VOCODER_STAGES];
  float z1c[VOCODER_BANDS][VOCODER_STAGES];
  float z2c[VOCODER_BANDS][VOCODER_STAGES];

  /* envelope per band */
  float env[VOCODER_BANDS];

  /* bark windowing */
  float bark_pos[VOCODER_BANDS];
  float bark_min, bark_max;

  /* smoothers */
  CParamSmooth smooth_mod_gain;
  CParamSmooth smooth_car_gain;
  CParamSmooth smooth_mix;
  CParamSmooth smooth_drive;
  CParamSmooth smooth_out_trim;

  CParamSmooth smooth_tilt;
  CParamSmooth smooth_center;
  CParamSmooth smooth_width;

  CParamSmooth smooth_atk_ms;
  CParamSmooth smooth_rel_ms;
  CParamSmooth smooth_env_curve;

  CParamSmooth smooth_band[VOCODER_BANDS];

  /* UI command mode */
  bool entering_command;
  char command_buffer[64];
  int command_index;

  pthread_mutex_t lock;
} Vocoder;

#endif
