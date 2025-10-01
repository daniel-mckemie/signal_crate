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

	clampf(&s->mix, 0.0f, 1.0f);
	clampf(&s->drive, 0.0f, 1.0f);
	clampf(&s->regen, 0.0f, 0.5f);
	clampf(&s->q, 0.5f, 40.0f);
	clampf(&s->tilt, -1.0f, 1.0f);
	clampf(&s->odd, -1.0f, 1.0f);

	if (s->bands < 1) s->bands = 1;
	if (s->bands > RES_MAX_BANDS) s->bands = RES_MAX_BANDS;
}
