#ifndef RES_BANK_H
#define RES_BANK_H

#include <pthread.h>
#include "util.h"

#define RES_MAX_BANDS 24

typedef struct {
	float sample_rate;

	float mix;
	float q;
	float lo_hz;
	float hi_hz;
	float tilt;
	float odd;
	float drive;
	float regen;
	int bands;

	float display_mix;
	float display_q;
	float display_lo_hz;
	float display_hi_hz;
	float display_tilt;
	float display_odd;
	float display_drive;
	float display_regen;
	int display_bands;

	CParamSmooth smooth_mix;
	CParamSmooth smooth_q;
	CParamSmooth smooth_lo_hz;
	CParamSmooth smooth_hi_hz;
	CParamSmooth smooth_tilt;
	CParamSmooth smooth_odd;
	CParamSmooth smooth_drive;
	CParamSmooth smooth_regen;
	CParamSmooth smooth_bands;
	
	float b0[RES_MAX_BANDS], b1[RES_MAX_BANDS], b2[RES_MAX_BANDS];
	float a1[RES_MAX_BANDS], a2[RES_MAX_BANDS];
	float z1[RES_MAX_BANDS], z2[RES_MAX_BANDS];

	float f[RES_MAX_BANDS];
	float w[RES_MAX_BANDS];

	int need_centers;
	int need_coeffs;

	bool entering_command;
	char command_buffer[64];
	int command_index;

	pthread_mutex_t lock;
} ResBank;

#endif
	

