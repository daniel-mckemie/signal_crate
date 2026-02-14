// vocoder.c
#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "vocoder.h"
#include "module.h"
#include "util.h"

static float bark_centers[VOCODER_BANDS] = {
    80,    120,   180,   260,   360,   510,   720,   1000,
    1400,  2000,  2800,  3700,  4800,  6200,  8000,  10000,
    12000, 14000, 16000, 18000, 20000, 22000, 24000, 26000};

static inline float hz_to_bark(float f) {
  return 13.0f * atanf(0.00076f * f) +
         3.5f * atanf((f / 7500.0f) * (f / 7500.0f));
}

static inline float tilt_gain(int i, float tilt) {
  float t = (float)i / (float)(VOCODER_BANDS - 1);
  return powf(2.0f, tilt * (t - 0.5f) * 4.0f);
}

static inline float center_window_bark(const Vocoder *s, int b,
                                       float center01, float width01) {
  float c = s->bark_min + center01 * (s->bark_max - s->bark_min);
  float span = (s->bark_max - s->bark_min);
  float sigma = fmaxf(width01, 0.02f) * span;
  float d = s->bark_pos[b] - c;
  return expf(-(d * d) / (2.0f * sigma * sigma));
}

static inline float soft_sat(float x, float drive) {
  if (drive <= 0.0f) return x;
  if (drive > 1.0f) drive = 1.0f;
  float k = 1.0f + 9.0f * drive;
  float y = tanhf(k * x);
  float n = tanhf(k);
  if (n > 1e-6f) y /= n;
  return y;
}

static inline float biquad_tick_state(const Vocoder *s, int b, int st,
                                      float x, float *z1, float *z2) {
  float y = s->b0[b][st] * x + *z1;
  *z1 = s->b1[b][st] * x + *z2 - s->a1[b][st] * y;
  *z2 = s->b2[b][st] * x - s->a2[b][st] * y;
  return y;
}

static inline float env_follow_ar(float env, float x, float sr,
                                 float atk_ms, float rel_ms) {
  float rect = fabsf(x);
  if (!isfinite(rect)) rect = 0.0f;

  float atk_s = fmaxf(atk_ms, 0.1f) * 0.001f;
  float rel_s = fmaxf(rel_ms, 1.0f) * 0.001f;

  float a = expf(-1.0f / (atk_s * sr));
  float r = expf(-1.0f / (rel_s * sr));

  if (rect > env) return a * env + (1.0f - a) * rect;
  return r * env + (1.0f - r) * rect;
}

static void rebuild_filters(Vocoder *s) {
  float ny = s->sample_rate * 0.45f;

  for (int i = 0; i < VOCODER_BANDS; i++) {
    float fc = bark_centers[i];
    if (fc > ny) fc = ny;
    s->fc[i] = fc;

    /* less peaky */
    s->Q[i] = 3.0f;

    for (int st = 0; st < VOCODER_STAGES; st++) {
      float omega = TWO_PI * fc / s->sample_rate;
      float sn = sinf(omega), cs = cosf(omega);
      float alpha = sn / (2.0f * s->Q[i]);

      /* RBJ bandpass; numerator scaled by Q */
      float b0 = alpha * s->Q[i];
      float b1 = 0.0f;
      float b2 = -alpha * s->Q[i];

      float a0 = 1.0f + alpha;
      float a1 = -2.0f * cs;
      float a2 = 1.0f - alpha;

      s->b0[i][st] = b0 / a0;
      s->b1[i][st] = b1 / a0;
      s->b2[i][st] = b2 / a0;
      s->a1[i][st] = a1 / a0;
      s->a2[i][st] = a2 / a0;
    }
  }
}

static void clamp_params(Vocoder *s) {
  clampf(&s->mod_gain, 0.0f, 2.0f);
  clampf(&s->car_gain, 0.0f, 2.0f);
  clampf(&s->wet, 0.0f, 1.0f);
  clampf(&s->dry, 0.0f, 1.0f);
  clampf(&s->drive, 0.0f, 1.0f);
  clampf(&s->out_trim, 0.0f, 2.0f);

  clampf(&s->tilt, -1.0f, 1.0f);
  clampf(&s->center, 0.0f, 1.0f);
  clampf(&s->width, 0.02f, 1.0f);

  clampf(&s->atk_ms, 0.1f, 200.0f);
  clampf(&s->rel_ms, 1.0f, 1000.0f);
  clampf(&s->env_curve, 0.25f, 4.0f);

  if (s->sel_band < 0) s->sel_band = 0;
  if (s->sel_band > VOCODER_BANDS - 1) s->sel_band = VOCODER_BANDS - 1;

  for (int i = 0; i < VOCODER_BANDS; i++) clampf(&s->band_gain[i], 0.0f, 2.0f);
}

static int parse_band_gain_param(const char *param) {
  if (!param) return -1;

  if (param[0] == 'b') {
    int idx = atoi(param + 1);
    if (idx >= 0 && idx < VOCODER_BANDS) return idx;
  }

  if (strncmp(param, "band", 4) == 0) {
    const char *p = param + 4;
    int n = atoi(p); /* 1..24 */
    if (n >= 1 && n <= VOCODER_BANDS) {
      if (strstr(param, "gain")) return n - 1;
    }
  }

  return -1;
}

static void vocoder_process(Module *m, float *in, unsigned long frames) {
  (void)in;
  Vocoder *s = (Vocoder *)m->state;

  float *mod = (m->num_inputs > 0) ? m->inputs[0] : NULL;  /* modulator */
  float *car = (m->num_inputs > 1) ? m->inputs[1] : NULL;  /* carrier */
  float *out = m->output_buffer;

  if (!mod && !car) {
    memset(out, 0, frames * sizeof(float));
    return;
  }
  if (!mod) mod = car;
  if (!car) car = mod;

  float base_band[VOCODER_BANDS];
  float base_mod_gain, base_car_gain, base_wet, base_dry, base_drive, base_out_trim;
  float base_tilt, base_center, base_width;
  float base_atk, base_rel, base_curve;

  pthread_mutex_lock(&s->lock);
  base_mod_gain = s->mod_gain;
  base_car_gain = s->car_gain;
  base_wet = s->wet;
  base_dry = s->dry;
  base_drive = s->drive;
  base_out_trim = s->out_trim;

  base_tilt = s->tilt;
  base_center = s->center;
  base_width = s->width;

  base_atk = s->atk_ms;
  base_rel = s->rel_ms;
  base_curve = s->env_curve;

  for (int b = 0; b < VOCODER_BANDS; b++) base_band[b] = s->band_gain[b];
  pthread_mutex_unlock(&s->lock);

  float mod_gain_s = process_smoother(&s->smooth_mod_gain, base_mod_gain);
  float car_gain_s = process_smoother(&s->smooth_car_gain, base_car_gain);
  float wet_s      = process_smoother(&s->smooth_wet, base_wet);
  float dry_s      = process_smoother(&s->smooth_dry, base_dry);
  float drive_s    = process_smoother(&s->smooth_drive, base_drive);
  float trim_s     = process_smoother(&s->smooth_out_trim, base_out_trim);

  float tilt_s     = process_smoother(&s->smooth_tilt, base_tilt);
  float center_s   = process_smoother(&s->smooth_center, base_center);
  float width_s    = process_smoother(&s->smooth_width, base_width);

  float atk_s      = process_smoother(&s->smooth_atk_ms, base_atk);
  float rel_s      = process_smoother(&s->smooth_rel_ms, base_rel);
  float curve_s    = process_smoother(&s->smooth_env_curve, base_curve);

  float disp_mod_gain = mod_gain_s, disp_car_gain = car_gain_s;
  float disp_wet = wet_s, disp_dry = dry_s, disp_drive = drive_s, disp_trim = trim_s;
  float disp_tilt = tilt_s, disp_center = center_s, disp_width = width_s;
  float disp_atk = atk_s, disp_rel = rel_s, disp_curve = curve_s;

  const float band_norm = 1.0f / sqrtf((float)VOCODER_BANDS);

  for (unsigned int i = 0; i < frames; i++) {
    float mod_gain = mod_gain_s;
    float car_gain = car_gain_s;
    float wet = wet_s;
    float dry = dry_s;
    float drive = drive_s;
    float out_trim = trim_s;

    float tilt = tilt_s;
    float center = center_s;
    float width = width_s;

    float atk_ms = atk_s;
    float rel_ms = rel_s;
    float env_curve = curve_s;

    float g_target[VOCODER_BANDS];
    for (int b = 0; b < VOCODER_BANDS; b++) g_target[b] = base_band[b];

    /* CV control inputs */
    for (int j = 0; j < m->num_control_inputs; j++) {
      if (!m->control_inputs[j] || !m->control_input_params[j]) continue;
      const char *param = m->control_input_params[j];
      float control = m->control_inputs[j][i];
      control = fminf(fmaxf(control, -1.0f), 1.0f);

      if (strcmp(param, "mod_gain") == 0) mod_gain += control;
      else if (strcmp(param, "car_gain") == 0) car_gain += control;
      else if (strcmp(param, "wet") == 0) wet += control;
      else if (strcmp(param, "dry") == 0) dry += control;
      else if (strcmp(param, "drive") == 0) drive += control;
      else if (strcmp(param, "trim") == 0 || strcmp(param, "out_trim") == 0) out_trim += control;

      else if (strcmp(param, "tilt") == 0) tilt += control;
      else if (strcmp(param, "center") == 0) center += control;
      else if (strcmp(param, "width") == 0) width += control;

      else if (strcmp(param, "atk") == 0 || strcmp(param, "atk_ms") == 0) atk_ms += 50.0f * control;
      else if (strcmp(param, "rel") == 0 || strcmp(param, "rel_ms") == 0) rel_ms += 200.0f * control;
      else if (strcmp(param, "curve") == 0 || strcmp(param, "env_curve") == 0) env_curve += control;

      else {
        int idx = parse_band_gain_param(param);
        if (idx >= 0) g_target[idx] += control;
      }
    }

    clampf(&mod_gain, 0.0f, 2.0f);
    clampf(&car_gain, 0.0f, 2.0f);
    clampf(&wet, 0.0f, 1.0f);
    clampf(&dry, 0.0f, 1.0f);
    clampf(&drive, 0.0f, 1.0f);
    clampf(&out_trim, 0.0f, 2.0f);

    clampf(&tilt, -1.0f, 1.0f);
    clampf(&center, 0.0f, 1.0f);
    clampf(&width, 0.02f, 1.0f);

    clampf(&atk_ms, 0.1f, 200.0f);
    clampf(&rel_ms, 1.0f, 1000.0f);
    clampf(&env_curve, 0.25f, 4.0f);

    disp_mod_gain = mod_gain;
    disp_car_gain = car_gain;
    disp_wet = wet;
    disp_dry = dry;
    disp_drive = drive;
    disp_trim = out_trim;

    disp_tilt = tilt;
    disp_center = center;
    disp_width = width;

    disp_atk = atk_ms;
    disp_rel = rel_ms;
    disp_curve = env_curve;

    float mx = mod[i] * mod_gain;
    float cx = car[i] * car_gain;
    if (!isfinite(mx)) mx = 0.0f;
    if (!isfinite(cx)) cx = 0.0f;

    float sum = 0.0f;

    /* modulator analysis -> env; carrier synthesis -> sum */
    for (int b = 0; b < VOCODER_BANDS; b++) {
      /* mod bandpass */
      float ym = mx;
      for (int st = 0; st < VOCODER_STAGES; st++) {
        ym = biquad_tick_state(s, b, st, ym, &s->z1m[b][st], &s->z2m[b][st]);
      }

      /* env */
      float e = s->env[b];
      e = env_follow_ar(e, ym, s->sample_rate, atk_ms, rel_ms);
      if (e < 1e-8f) e = 0.0f;
      s->env[b] = e;

      /* carrier bandpass */
      float yc = cx;
      for (int st = 0; st < VOCODER_STAGES; st++) {
        yc = biquad_tick_state(s, b, st, yc, &s->z1c[b][st], &s->z2c[b][st]);
      }

      /* apply envelope */
      float env01 = e / (e + 0.5f);
      if (env01 < 0.0f) env01 = 0.0f;
      float env_shaped = powf(env01, env_curve);

      float g = g_target[b];
      clampf(&g, 0.0f, 2.0f);
      float g_s = process_smoother(&s->smooth_band[b], g);

      float w = center_window_bark(s, b, center, width);
      float t = tilt_gain(b, tilt);

      sum += yc * env_shaped * g_s * w * t;
    }

    /* gain staging / normalization */
    float wet_pre = sum * band_norm * out_trim;
    float wet_out = soft_sat(wet_pre, drive);

    float y = wet * wet_out + dry * cx;
    out[i] = fminf(fmaxf(y, -1.0f), 1.0f);
  }

  pthread_mutex_lock(&s->lock);
  s->display_mod_gain = disp_mod_gain;
  s->display_car_gain = disp_car_gain;
  s->display_wet = disp_wet;
  s->display_dry = disp_dry;
  s->display_drive = disp_drive;
  s->display_out_trim = disp_trim;

  s->display_tilt = disp_tilt;
  s->display_center = disp_center;
  s->display_width = disp_width;

  s->display_atk_ms = disp_atk;
  s->display_rel_ms = disp_rel;
  s->display_env_curve = disp_curve;

  s->display_sel_gain = s->band_gain[s->sel_band];
  pthread_mutex_unlock(&s->lock);
}

static void vocoder_draw_ui(Module *m, int y, int x) {
  Vocoder *s = (Vocoder *)m->state;

  float mg, cg, wet, dry, drive, trim, tilt, center, width, atk, rel, curve;
  int sb;
  float sg;

  pthread_mutex_lock(&s->lock);
  mg = s->display_mod_gain;
  cg = s->display_car_gain;
  wet = s->display_wet;
  dry = s->display_dry;
  drive = s->display_drive;
  trim = s->display_out_trim;

  tilt = s->display_tilt;
  center = s->display_center;
  width = s->display_width;

  atk = s->display_atk_ms;
  rel = s->display_rel_ms;
  curve = s->display_env_curve;

  sb = s->sel_band;
  sg = s->display_sel_gain;
  pthread_mutex_unlock(&s->lock);

  BLUE();
  mvprintw(y, x, "[Vocoder:%s] ", m->name);
  CLR();

  LABEL(2, "mg:"); ORANGE(); printw("%.2f", mg); CLR();
  LABEL(2, "cg:"); ORANGE(); printw("%.2f", cg); CLR();
  LABEL(2, "wet:"); ORANGE(); printw("%.2f", wet); CLR();
  LABEL(2, "dry:"); ORANGE(); printw("%.2f", dry); CLR();
  LABEL(2, "d:"); ORANGE(); printw("%.2f", drive); CLR();
  LABEL(2, "tr:"); ORANGE(); printw("%.2f", trim); CLR();

  LABEL(2, "t:"); ORANGE(); printw("%.2f", tilt); CLR();
  LABEL(2, "c:"); ORANGE(); printw("%.2f", center); CLR();
  LABEL(2, "w:"); ORANGE(); printw("%.2f", width); CLR();

  LABEL(2, "a:"); ORANGE(); printw("%.1f", atk); CLR();
  LABEL(2, "r:"); ORANGE(); printw("%.1f", rel); CLR();
  LABEL(2, "cv:"); ORANGE(); printw("%.2f", curve); CLR();

  LABEL(2, "b:"); ORANGE(); printw("%02d", sb); CLR();
  LABEL(2, "g:"); ORANGE(); printw("%.2f", sg); CLR();

  YELLOW();
  mvprintw(y + 1, x,
           "-/= mg _/+ cg [/] wet {/} dry ,/. drive </> trim 8/9 tilt 5/6 c 1/2 w"
           " a/z atk s/x rel c/v curve b/B band ;/' gain");
  mvprintw(y + 2, x,
           ":1[mg] :2[cg] :3[wet] :4[dry] :5[drive] :t[trim] :6[tilt] :7[c] :8[w]"
           " :9[atk] :0[rel] :q[curve] :b[band] :g[gain]");
  BLACK();
}

static void vocoder_handle_input(Module *m, int key) {
  Vocoder *s = (Vocoder *)m->state;
  int handled = 0;

  pthread_mutex_lock(&s->lock);
  if (!s->entering_command) {
    switch (key) {
    case '=': s->mod_gain += 0.01f; handled = 1; break;
    case '-': s->mod_gain -= 0.01f; handled = 1; break;

    case '+': s->car_gain += 0.01f; handled = 1; break;
    case '_': s->car_gain -= 0.01f; handled = 1; break;

    case ']': s->wet += 0.01f; handled = 1; break;
    case '[': s->wet -= 0.01f; handled = 1; break;

    case '}': s->dry += 0.01f; handled = 1; break;
    case '{': s->dry -= 0.01f; handled = 1; break;

    case '.': s->drive += 0.01f; handled = 1; break;
    case ',': s->drive -= 0.01f; handled = 1; break;

    case '>': s->out_trim += 0.01f; handled = 1; break;
    case '<': s->out_trim -= 0.01f; handled = 1; break;

    case '9': s->tilt += 0.01f; handled = 1; break;
    case '8': s->tilt -= 0.01f; handled = 1; break;

    case '6': s->center += 0.01f; handled = 1; break;
    case '5': s->center -= 0.01f; handled = 1; break;

    case '2': s->width += 0.01f; handled = 1; break;
    case '1': s->width -= 0.01f; handled = 1; break;

    case 'a': s->atk_ms += 1.0f; handled = 1; break;
    case 'z': s->atk_ms -= 1.0f; handled = 1; break;

    case 's': s->rel_ms += 5.0f; handled = 1; break;
    case 'x': s->rel_ms -= 5.0f; handled = 1; break;

    case 'c': s->env_curve += 0.05f; handled = 1; break;
    case 'v': s->env_curve -= 0.05f; handled = 1; break;

    case 'B': s->sel_band += 1; handled = 1; break;
    case 'b': s->sel_band -= 1; handled = 1; break;

    case '\'': s->band_gain[s->sel_band] += 0.01f; handled = 1; break;
    case ';':  s->band_gain[s->sel_band] -= 0.01f; handled = 1; break;

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
      if (sscanf(s->command_buffer, "%c %f", &type, &val) >= 1) {
        if (type == '1') s->mod_gain = val;
        else if (type == '2') s->car_gain = val;
        else if (type == '3') s->wet = val;
        else if (type == '4') s->dry = val;
        else if (type == '5') s->drive = val;
        else if (type == 't') s->out_trim = val;
        else if (type == '6') s->tilt = val;
        else if (type == '7') s->center = val;
        else if (type == '8') s->width = val;
        else if (type == '9') s->atk_ms = val;
        else if (type == '0') s->rel_ms = val;
        else if (type == 'q') s->env_curve = val;
        else if (type == 'b') s->sel_band = (int)val;
        else if (type == 'g') s->band_gain[s->sel_band] = val;
      }
      handled = 1;
    } else if (key == 27) {
      s->entering_command = false;
      handled = 1;
    } else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
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

  if (handled) clamp_params(s);
  pthread_mutex_unlock(&s->lock);
}

static void vocoder_set_osc_param(Module *m, const char *param, float value) {
  Vocoder *s = (Vocoder *)m->state;
  pthread_mutex_lock(&s->lock);

  if (strcmp(param, "mod_gain") == 0) s->mod_gain = value;
  else if (strcmp(param, "car_gain") == 0) s->car_gain = value;
  else if (strcmp(param, "wet") == 0) s->wet = value;
  else if (strcmp(param, "dry") == 0) s->dry = value;
  else if (strcmp(param, "drive") == 0) s->drive = value;
  else if (strcmp(param, "trim") == 0 || strcmp(param, "out_trim") == 0) s->out_trim = value;

  else if (strcmp(param, "tilt") == 0) s->tilt = value;
  else if (strcmp(param, "center") == 0) s->center = value;
  else if (strcmp(param, "width") == 0) s->width = value;

  else if (strcmp(param, "atk") == 0 || strcmp(param, "atk_ms") == 0) s->atk_ms = value;
  else if (strcmp(param, "rel") == 0 || strcmp(param, "rel_ms") == 0) s->rel_ms = value;
  else if (strcmp(param, "curve") == 0 || strcmp(param, "env_curve") == 0) s->env_curve = value;

  else {
    int idx = parse_band_gain_param(param);
    if (idx >= 0) s->band_gain[idx] = value;
  }

  clamp_params(s);
  pthread_mutex_unlock(&s->lock);
}

static void vocoder_destroy(Module *m) {
  Vocoder *s = (Vocoder *)m->state;
  if (s) pthread_mutex_destroy(&s->lock);
  destroy_base_module(m);
}

__attribute__((visibility("default")))
Module *create_module(const char *args, float sample_rate) {
  Vocoder *s = calloc(1, sizeof(Vocoder));
  s->sample_rate = sample_rate;

  /* defaults (less peaky) */
  s->mod_gain = 0.70f;
  s->car_gain = 0.70f;
  s->wet = 1.0f;
  s->dry = 0.0f;
  s->drive = 0.02f;
  s->out_trim = 0.85f;

  s->tilt = 0.0f;
  s->center = 0.5f;
  s->width = 1.0f;

  s->atk_ms = 5.0f;
  s->rel_ms = 80.0f;
  s->env_curve = 1.5f;

  s->sel_band = 0;

  for (int i = 0; i < VOCODER_BANDS; i++) {
    s->band_gain[i] = 1.0f;
    s->env[i] = 0.0f;
  }

  /* args */
  if (args && strstr(args, "mod_gain="))  sscanf(strstr(args, "mod_gain="),  "mod_gain=%f", &s->mod_gain);
  if (args && strstr(args, "car_gain="))  sscanf(strstr(args, "car_gain="),  "car_gain=%f", &s->car_gain);
  if (args && strstr(args, "wet="))       sscanf(strstr(args, "wet="),       "wet=%f", &s->wet);
  if (args && strstr(args, "dry="))       sscanf(strstr(args, "dry="),       "dry=%f", &s->dry);
  if (args && strstr(args, "drive="))     sscanf(strstr(args, "drive="),     "drive=%f", &s->drive);
  if (args && strstr(args, "trim="))      sscanf(strstr(args, "trim="),      "trim=%f", &s->out_trim);
  if (args && strstr(args, "out_trim="))  sscanf(strstr(args, "out_trim="),  "out_trim=%f", &s->out_trim);

  if (args && strstr(args, "tilt="))      sscanf(strstr(args, "tilt="),      "tilt=%f", &s->tilt);
  if (args && strstr(args, "center="))    sscanf(strstr(args, "center="),    "center=%f", &s->center);
  if (args && strstr(args, "width="))     sscanf(strstr(args, "width="),     "width=%f", &s->width);

  if (args && strstr(args, "atk_ms="))    sscanf(strstr(args, "atk_ms="),    "atk_ms=%f", &s->atk_ms);
  if (args && strstr(args, "rel_ms="))    sscanf(strstr(args, "rel_ms="),    "rel_ms=%f", &s->rel_ms);
  if (args && strstr(args, "curve="))     sscanf(strstr(args, "curve="),     "curve=%f", &s->env_curve);

  pthread_mutex_init(&s->lock, NULL);

  init_smoother(&s->smooth_mod_gain, 0.50f);
  init_smoother(&s->smooth_car_gain, 0.50f);
  init_smoother(&s->smooth_wet,      0.50f);
  init_smoother(&s->smooth_dry,      0.50f);
  init_smoother(&s->smooth_drive,    0.50f);
  init_smoother(&s->smooth_out_trim, 0.50f);

  init_smoother(&s->smooth_tilt,     0.50f);
  init_smoother(&s->smooth_center,   0.75f);
  init_smoother(&s->smooth_width,    0.75f);

  init_smoother(&s->smooth_atk_ms,   0.50f);
  init_smoother(&s->smooth_rel_ms,   0.50f);
  init_smoother(&s->smooth_env_curve,0.50f);

  for (int i = 0; i < VOCODER_BANDS; i++) init_smoother(&s->smooth_band[i], 0.50f);

  clamp_params(s);
  rebuild_filters(s);

  for (int i = 0; i < VOCODER_BANDS; i++) s->bark_pos[i] = hz_to_bark(bark_centers[i]);
  s->bark_min = s->bark_pos[0];
  s->bark_max = s->bark_pos[VOCODER_BANDS - 1];

  /* init display */
  s->display_mod_gain = s->mod_gain;
  s->display_car_gain = s->car_gain;
  s->display_wet = s->wet;
  s->display_dry = s->dry;
  s->display_drive = s->drive;
  s->display_out_trim = s->out_trim;

  s->display_tilt = s->tilt;
  s->display_center = s->center;
  s->display_width = s->width;

  s->display_atk_ms = s->atk_ms;
  s->display_rel_ms = s->rel_ms;
  s->display_env_curve = s->env_curve;

  s->display_sel_gain = s->band_gain[s->sel_band];

  Module *m = calloc(1, sizeof(Module));
  m->name = "vocoder";
  m->state = s;

  /* IN0 = modulator, IN1 = carrier */
  m->num_inputs = 2;

  m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
  m->process = vocoder_process;
  m->draw_ui = vocoder_draw_ui;
  m->handle_input = vocoder_handle_input;
  m->set_param = vocoder_set_osc_param;
  m->destroy = vocoder_destroy;

  return m;
}

