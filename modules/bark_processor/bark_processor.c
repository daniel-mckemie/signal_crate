#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "bark_processor.h"
#include "module.h"
#include "util.h"

static float bark_centers[BARK_PROC_BANDS] = {
    80,    120,   180,   260,   360,   510,   720,   1000,
    1400,  2000,  2800,  3700,  4800,  6200,  8000,  10000,
    12000, 14000, 16000, 18000, 20000, 22000, 24000, 26000};

static inline float hz_to_bark(float f) {
  return 13.0f * atanf(0.00076f * f) +
         3.5f * atanf((f / 7500.0f) * (f / 7500.0f));
}

static inline float tilt_gain(int i, float tilt) {
  float t = (float)i / (float)(BARK_PROC_BANDS - 1);
  return powf(2.0f, tilt * (t - 0.5f) * 4.0f);
}

static inline float center_window_bark(const BarkProcessor *s, int b,
                                       float center01, float width01) {
  float c = s->bark_min + center01 * (s->bark_max - s->bark_min);
  float span = (s->bark_max - s->bark_min);
  float sigma = fmaxf(width01, 0.02f) * span;
  float d = s->bark_pos[b] - c;
  return expf(-(d * d) / (2.0f * sigma * sigma));
}

static inline float soft_sat(float x, float drive) {
  if (drive <= 0.0f)
    return x; /* true bypass */
  if (drive >= 1.0f)
    drive = 1.0f;

  /* k: 1..10 */
  float k = 1.0f + 9.0f * drive;

  /* tanh saturator, normalized so unity-ish */
  float y = tanhf(k * x);
  float n = tanhf(k);
  if (n > 1e-6f)
    y /= n;
  return y;
}

static inline float biquad_tick_state(const BarkProcessor *s, int b, int st,
                                      float x, float *z1, float *z2) {
  float y = s->b0[b][st] * x + *z1;
  *z1 = s->b1[b][st] * x + *z2 - s->a1[b][st] * y;
  *z2 = s->b2[b][st] * x - s->a2[b][st] * y;
  return y;
}

/* Verbos-style follower: instant attack, fixed release (~80ms). No user atk/rel
 * params. */
static inline float env_follow(float env, float x, float sr) {
  float rect = fabsf(x);
  if (!isfinite(rect))
    rect = 0.0f;

  /* ~80ms release */
  const float r = expf(-1.0f / (0.08f * sr));
  if (rect > env)
    return rect;
  return r * env + (1.0f - r) * rect;
}

static void rebuild_filters(BarkProcessor *s) {
  float ny = s->sample_rate * 0.45f;

  for (int i = 0; i < BARK_PROC_BANDS; i++) {
    float fc = bark_centers[i];
    if (fc > ny)
      fc = ny;

    s->fc[i] = fc;

    /* wider, more “filterbank-y” than vocoder-Q */
    s->Q[i] = 0.7f;

    for (int st = 0; st < BARK_PROC_STAGES; st++) {
      float omega = TWO_PI * fc / s->sample_rate;
      float sn = sinf(omega), cs = cosf(omega);
      float alpha = sn / (2.0f * s->Q[i]);

      /* RBJ bandpass; scale numerator by Q to keep output energetic */
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

static void clamp_params(BarkProcessor *s) {
  clampf(&s->center, 0.0f, 1.0f);
  clampf(&s->width, 0.02f, 1.0f);
  clampf(&s->tilt, -1.0f, 1.0f);
  clampf(&s->drive, 0.0f, 1.0f);

  clampf(&s->out_gain_odd, 0.0f, 1.0f);
  clampf(&s->out_gain_even, 0.0f, 1.0f);

  if (s->odd_to_even)
    s->odd_to_even = 1;
  if (s->even_to_odd)
    s->even_to_odd = 1;

  if (s->sel_band < 0)
    s->sel_band = 0;
  if (s->sel_band > BARK_PROC_BANDS - 1)
    s->sel_band = BARK_PROC_BANDS - 1;

  for (int i = 0; i < BARK_PROC_BANDS; i++)
    clampf(&s->band_gain[i], 0.0f, 2.0f);
}

static int parse_band_gain_param(const char *param) {
  if (!param)
    return -1;

  /* b0..b23 */
  if (param[0] == 'b') {
    int idx = atoi(param + 1);
    if (idx >= 0 && idx < BARK_PROC_BANDS)
      return idx;
  }

  /* band1gain..band24gain */
  if (strncmp(param, "band", 4) == 0) {
    const char *p = param + 4;
    int n = atoi(p); /* 1..24 */
    if (n >= 1 && n <= BARK_PROC_BANDS) {
      if (strstr(param, "gain"))
        return n - 1;
    }
  }

  return -1;
}

static void bark_processor_process(Module *m, float *in, unsigned long frames) {
  (void)in;
  BarkProcessor *s = (BarkProcessor *)m->state;

  /* Verbos-style: input A excites odd bands, input B excites even bands */
  float *in_odd = (m->num_inputs > 0) ? m->inputs[0] : NULL;  /* IN A */
  float *in_even = (m->num_inputs > 1) ? m->inputs[1] : NULL; /* IN B */
  float *out = m->output_buffer;

  if (!in_odd && !in_even) {
    memset(out, 0, frames * sizeof(float));
    return;
  }
  if (!in_odd)
    in_odd = in_even;
  if (!in_even)
    in_even = in_odd;

  float base_band[BARK_PROC_BANDS];
  float base_center, base_width, base_tilt, base_drive;
  float base_og_odd, base_og_even;
  int base_o2e, base_e2o;

  pthread_mutex_lock(&s->lock);
  base_center = s->center;
  base_width = s->width;
  base_tilt = s->tilt;
  base_drive = s->drive;
  base_og_odd = s->out_gain_odd;
  base_og_even = s->out_gain_even;
  base_o2e = s->odd_to_even;
  base_e2o = s->even_to_odd;
  for (int b = 0; b < BARK_PROC_BANDS; b++)
    base_band[b] = s->band_gain[b];
  pthread_mutex_unlock(&s->lock);

  float center_s = process_smoother(&s->smooth_center, base_center);
  float width_s = process_smoother(&s->smooth_width, base_width);
  float tilt_s = process_smoother(&s->smooth_tilt, base_tilt);
  float drive_s = process_smoother(&s->smooth_drive, base_drive);
  float og_odd_s = process_smoother(&s->smooth_out_gain_odd, base_og_odd);
  float og_even_s = process_smoother(&s->smooth_out_gain_even, base_og_even);

  float disp_center = center_s;
  float disp_width = width_s;
  float disp_tilt = tilt_s;
  float disp_drive = drive_s;
  float disp_og_odd = og_odd_s;
  float disp_og_even = og_even_s;
  int disp_o2e = base_o2e;
  int disp_e2o = base_e2o;

  for (unsigned int i = 0; i < frames; i++) {
    float center = center_s;
    float width = width_s;
    float tilt = tilt_s;
    float drive = drive_s;
    float og_odd = og_odd_s;
    float og_even = og_even_s;
    int o2e = base_o2e;
    int e2o = base_e2o;

    float g_target[BARK_PROC_BANDS];
    for (int b = 0; b < BARK_PROC_BANDS; b++)
      g_target[b] = base_band[b];

    /* CV control inputs */
    for (int j = 0; j < m->num_control_inputs; j++) {
      if (!m->control_inputs[j] || !m->control_input_params[j])
        continue;
      const char *param = m->control_input_params[j];
      float control = m->control_inputs[j][i];
      control = fminf(fmaxf(control, -1.0f), 1.0f);

      if (strcmp(param, "center") == 0)
        center += control;
      else if (strcmp(param, "width") == 0)
        width += control;
      else if (strcmp(param, "tilt") == 0)
        tilt += control;
      else if (strcmp(param, "drive") == 0)
        drive += control;

      else if (strcmp(param, "out_odd") == 0 ||
               strcmp(param, "outgain_odd") == 0)
        og_odd += control;
      else if (strcmp(param, "out_even") == 0 ||
               strcmp(param, "outgain_even") == 0)
        og_even += control;

      else if (strcmp(param, "odd2even") == 0)
        o2e = (control > 0.0f);
      else if (strcmp(param, "even2odd") == 0)
        e2o = (control > 0.0f);

      else {
        int idx = parse_band_gain_param(param);
        if (idx >= 0)
          g_target[idx] += control;
      }
    }

    clampf(&center, 0.0f, 1.0f);
    clampf(&width, 0.02f, 1.0f);
    clampf(&tilt, -1.0f, 1.0f);
    clampf(&drive, 0.0f, 1.0f);
    clampf(&og_odd, 0.0f, 1.0f);
    clampf(&og_even, 0.0f, 1.0f);
    o2e = o2e ? 1 : 0;
    e2o = e2o ? 1 : 0;

    disp_center = center;
    disp_width = width;
    disp_tilt = tilt;
    disp_drive = drive;
    disp_og_odd = og_odd;
    disp_og_even = og_even;
    disp_o2e = o2e;
    disp_e2o = e2o;

    float sum = 0.0f;

    for (int b = 0; b < BARK_PROC_BANDS; b++) {
      float x = (b & 1) ? in_odd[i] : in_even[i];
      if (!isfinite(x))
        x = 0.0f;

      float yc = x;
      for (int st = 0; st < BARK_PROC_STAGES; st++) {
        yc = biquad_tick_state(s, b, st, yc, &s->z1[b][st], &s->z2[b][st]);
      }

      /* update envelope per band */
      float e = s->env[b];
      e = env_follow(e, yc, s->sample_rate);
      if (e < 1e-8f)
        e = 0.0f;
      s->env[b] = e;

      /* cross-modulation: neighbor band in the other bank */
      int pair = (b & 1) ? (b - 1) : (b + 1);
      if (pair < 0 || pair >= BARK_PROC_BANDS)
        pair = b;

      float mod = 1.0f;
      if (o2e && !(b & 1)) {
        float mval = s->env[pair];             /* 0..~ */
        mval = fminf(fmaxf(mval, 0.0f), 1.0f); /* clamp */
        mval = mval * mval;                    /* curve: more separation */
        mod *= mval;
      }
      if (e2o && (b & 1)) {
        float mval = s->env[pair];             /* 0..~ */
        mval = fminf(fmaxf(mval, 0.0f), 1.0f); /* clamp */
        mval = mval * mval;                    /* curve: more separation */
        mod *= mval;
      }
      yc *= mod;

      float g = g_target[b];
      clampf(&g, 0.0f, 2.0f);
      float g_s = process_smoother(&s->smooth_band[b], g);

      float w = center_window_bark(s, b, center, width);
      float t = tilt_gain(b, tilt);

      float bank_gain = (b & 1) ? og_odd : og_even;

      float wt = g_s * w * t * bank_gain;
      sum += yc * wt * bank_gain;
    }

    out[i] = fminf(fmaxf(soft_sat(sum, drive), -1.0f), 1.0f);
  }

  pthread_mutex_lock(&s->lock);
  s->display_center = disp_center;
  s->display_width = disp_width;
  s->display_tilt = disp_tilt;
  s->display_drive = disp_drive;
  s->display_out_gain_odd = disp_og_odd;
  s->display_out_gain_even = disp_og_even;
  s->display_odd_to_even = disp_o2e;
  s->display_even_to_odd = disp_e2o;
  s->display_sel_gain = s->band_gain[s->sel_band];
  pthread_mutex_unlock(&s->lock);
}

static void bark_processor_draw_ui(Module *m, int y, int x) {
  BarkProcessor *s = (BarkProcessor *)m->state;

  float center, width, tilt, drive, ogo, oge;
  int o2e, e2o;
  int sb;
  float sg;
  char cmd[64] = "";

  pthread_mutex_lock(&s->lock);
  center = s->display_center;
  width = s->display_width;
  tilt = s->display_tilt;
  drive = s->display_drive;
  ogo = s->display_out_gain_odd;
  oge = s->display_out_gain_even;
  o2e = s->display_odd_to_even;
  e2o = s->display_even_to_odd;
  sb = s->sel_band;
  sg = s->display_sel_gain;
  if (s->entering_command)
    snprintf(cmd, sizeof(cmd), ":%s", s->command_buffer);
  pthread_mutex_unlock(&s->lock);

  BLUE();
  mvprintw(y, x, "[BarkProc:%s] ", m->name);
  CLR();

  LABEL(2, "c:");
  ORANGE();
  printw("%.2f", center);
  CLR();
  LABEL(2, "w:");
  ORANGE();
  printw("%.2f", width);
  CLR();
  LABEL(2, "t:");
  ORANGE();
  printw("%.2f", tilt);
  CLR();
  LABEL(2, "d:");
  ORANGE();
  printw("%.2f", drive);
  CLR();
  LABEL(2, "og:");
  ORANGE();
  printw("%.2f", ogo);
  CLR();
  LABEL(2, "eg:");
  ORANGE();
  printw("%.2f", oge);
  CLR();
  LABEL(2, "o2e:");
  ORANGE();
  printw("%d", o2e);
  CLR();
  LABEL(2, "e2o:");
  ORANGE();
  printw("%d", e2o);
  CLR();
  LABEL(2, "b:");
  ORANGE();
  printw("%02d", sb);
  CLR();
  LABEL(2, "g:");
  ORANGE();
  printw("%.2f", sg);
  CLR();

  YELLOW();
  mvprintw(y + 1, x,
           "-/= c _/+ w [/] t {/} d i/I og k/K eg o o2e"
           " p e2o b/B b ;/' g");
  mvprintw(y + 2, x,
           ":1[c] :2[w] :3[t] :4[d] :5[og] :6[eg] :7[o2e] "
           ":8[e2o] :9[b] :0[g]");
  BLACK();
}

static void bark_processor_handle_input(Module *m, int key) {
  BarkProcessor *s = (BarkProcessor *)m->state;
  int handled = 0;

  pthread_mutex_lock(&s->lock);
  if (!s->entering_command) {
    switch (key) {
    case '=':
      s->center += 0.01f;
      handled = 1;
      break;
    case '-':
      s->center -= 0.01f;
      handled = 1;
      break;
    case '+':
      s->width += 0.01f;
      handled = 1;
      break;
    case '_':
      s->width -= 0.01f;
      handled = 1;
      break;
    case ']':
      s->tilt += 0.01f;
      handled = 1;
      break;
    case '[':
      s->tilt -= 0.01f;
      handled = 1;
      break;
    case '}':
      s->drive += 0.01f;
      handled = 1;
      break;
    case '{':
      s->drive -= 0.01f;
      handled = 1;
      break;

    case 'I':
      s->out_gain_odd += 0.01f;
      handled = 1;
      break;
    case 'i':
      s->out_gain_odd -= 0.01f;
      handled = 1;
      break;
    case 'K':
      s->out_gain_even += 0.01f;
      handled = 1;
      break;
    case 'k':
      s->out_gain_even -= 0.01f;
      handled = 1;
      break;

    case 'o':
      s->odd_to_even ^= 1;
      handled = 1;
      break;
    case 'p':
      s->even_to_odd ^= 1;
      handled = 1;
      break;

    case 'B':
      s->sel_band += 1;
      handled = 1;
      break;
    case 'b':
      s->sel_band -= 1;
      handled = 1;
      break;
    case '\'':
      s->band_gain[s->sel_band] += 0.01f;
      handled = 1;
      break;
    case ';':
      s->band_gain[s->sel_band] -= 0.01f;
      handled = 1;
      break;

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
        if (type == '1')
          s->center = val;
        else if (type == '2')
          s->width = val;
        else if (type == '3')
          s->tilt = val;
        else if (type == '4')
          s->drive = val;
        else if (type == '5')
          s->out_gain_odd = val;
        else if (type == '6')
          s->out_gain_even = val;
        else if (type == '7')
          s->odd_to_even = (val > 0.5f);
        else if (type == '8')
          s->even_to_odd = (val > 0.5f);
        else if (type == '9')
          s->sel_band = (int)val;
        else if (type == '0')
          s->band_gain[s->sel_band] = val;
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

  if (handled)
    clamp_params(s);
  pthread_mutex_unlock(&s->lock);
}

static void bark_processor_set_osc_param(Module *m, const char *param,
                                         float value) {
  BarkProcessor *s = (BarkProcessor *)m->state;
  pthread_mutex_lock(&s->lock);

  if (strcmp(param, "center") == 0)
    s->center = value;
  else if (strcmp(param, "width") == 0)
    s->width = value;
  else if (strcmp(param, "tilt") == 0)
    s->tilt = value;
  else if (strcmp(param, "drive") == 0)
    s->drive = value;

  else if (strcmp(param, "in_odd") == 0 || strcmp(param, "ingain_odd") == 0)
    s->out_gain_odd = value;
  else if (strcmp(param, "in_even") == 0 || strcmp(param, "ingain_even") == 0)
    s->out_gain_even = value;

  else if (strcmp(param, "odd2even") == 0)
    s->odd_to_even = (value > 0.5f);
  else if (strcmp(param, "even2odd") == 0)
    s->even_to_odd = (value > 0.5f);

  else {
    int idx = parse_band_gain_param(param);
    if (idx >= 0)
      s->band_gain[idx] = value;
  }

  clamp_params(s);
  pthread_mutex_unlock(&s->lock);
}

static void bark_processor_destroy(Module *m) {
  BarkProcessor *s = (BarkProcessor *)m->state;
  if (s)
    pthread_mutex_destroy(&s->lock);
  destroy_base_module(m);
}

Module *create_module(const char *args, float sample_rate) {
  BarkProcessor *s = calloc(1, sizeof(BarkProcessor));
  s->sample_rate = sample_rate;

  /* defaults */
  s->center = 0.5f;
  s->width = 0.15f;
  s->tilt = 0.0f;
  s->drive = 0.1f;

  s->out_gain_odd = 1.0f;
  s->out_gain_even = 1.0f;

  s->odd_to_even = 0;
  s->even_to_odd = 0;

  s->sel_band = 0;

  for (int i = 0; i < BARK_PROC_BANDS; i++) {
    s->band_gain[i] = 1.0f;
    s->env[i] = 0.0f;
  }

  /* args */
  if (args && strstr(args, "center="))
    sscanf(strstr(args, "center="), "center=%f", &s->center);
  if (args && strstr(args, "width="))
    sscanf(strstr(args, "width="), "width=%f", &s->width);
  if (args && strstr(args, "tilt="))
    sscanf(strstr(args, "tilt="), "tilt=%f", &s->tilt);
  if (args && strstr(args, "drive="))
    sscanf(strstr(args, "drive="), "drive=%f", &s->drive);

  if (args && strstr(args, "in_odd="))
    sscanf(strstr(args, "in_odd="), "in_odd=%f", &s->out_gain_odd);
  if (args && strstr(args, "in_even="))
    sscanf(strstr(args, "in_even="), "in_even=%f", &s->out_gain_even);

  if (args && strstr(args, "odd2even="))
    sscanf(strstr(args, "odd2even="), "odd2even=%d", &s->odd_to_even);
  if (args && strstr(args, "even2odd="))
    sscanf(strstr(args, "even2odd="), "even2odd=%d", &s->even_to_odd);

  pthread_mutex_init(&s->lock, NULL);

  init_smoother(&s->smooth_center, 0.75f);
  init_smoother(&s->smooth_width, 0.75f);
  init_smoother(&s->smooth_tilt, 0.50f);
  init_smoother(&s->smooth_drive, 0.50f);
  init_smoother(&s->smooth_out_gain_odd, 0.50f);
  init_smoother(&s->smooth_out_gain_even, 0.50f);
  for (int i = 0; i < BARK_PROC_BANDS; i++)
    init_smoother(&s->smooth_band[i], 0.50f);

  clamp_params(s);
  rebuild_filters(s);

  for (int i = 0; i < BARK_PROC_BANDS; i++) {
    s->bark_pos[i] = hz_to_bark(bark_centers[i]);
  }
  s->bark_min = s->bark_pos[0];
  s->bark_max = s->bark_pos[BARK_PROC_BANDS - 1];

  /* init display */
  s->display_center = s->center;
  s->display_width = s->width;
  s->display_tilt = s->tilt;
  s->display_drive = s->drive;
  s->display_out_gain_odd = s->out_gain_odd;
  s->display_out_gain_even = s->out_gain_even;
  s->display_odd_to_even = s->odd_to_even;
  s->display_even_to_odd = s->even_to_odd;
  s->display_sel_gain = s->band_gain[s->sel_band];

  Module *m = calloc(1, sizeof(Module));
  m->name = "bark_processor";
  m->state = s;

  /* Verbos-style: 2 audio inputs (odd bank, even bank) */
  m->num_inputs = 2;

  m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
  m->process = bark_processor_process;
  m->draw_ui = bark_processor_draw_ui;
  m->handle_input = bark_processor_handle_input;
  m->set_param = bark_processor_set_osc_param;
  m->destroy = bark_processor_destroy;

  return m;
}
