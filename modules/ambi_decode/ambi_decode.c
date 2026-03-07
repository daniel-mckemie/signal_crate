#include <math.h>
#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "ambi_decode.h"
#include "module.h"
#include "util.h"

#ifndef M_PI
#endif

static inline void clamp_params(AmbiDecode *s) {
  clampf(&s->azimuth, 0.0f, 360.0f);
  clampf(&s->elevation, -90.0f, 90.0f);
  clampf(&s->gain, 0.0f, 1.0f);
  clampf(&s->width, 0.0f, 1.0f);
}

static void ambi_decode_process(Module *m, float *in, unsigned long frames) {
  (void)in;
  AmbiDecode *s = (AmbiDecode *)m->state;
  float *out = m->output_buffer;

  // AmbiX input order from e_ambi_a_to_b: W, Y, Z, X
  float *w_ch = (m->num_inputs > 0) ? m->inputs[0] : NULL;
  float *y_ch = (m->num_inputs > 1) ? m->inputs[1] : NULL;
  float *z_ch = (m->num_inputs > 2) ? m->inputs[2] : NULL;
  float *x_ch = (m->num_inputs > 3) ? m->inputs[3] : NULL;

  if (!w_ch) {
    memset(out, 0, frames * sizeof(float));
    return;
  }

  pthread_mutex_lock(&s->lock);
  float base_azimuth = s->azimuth;
  float base_elevation = s->elevation;
  float base_gain = s->gain;
  float base_width = s->width;
  AmbiChannel channel = s->channel;
  pthread_mutex_unlock(&s->lock);

  float azimuth_s = process_smoother(&s->smooth_azimuth, base_azimuth);
  float elevation_s = process_smoother(&s->smooth_elevation, base_elevation);
  float gain_s = process_smoother(&s->smooth_gain, base_gain);
  float width_s = process_smoother(&s->smooth_width, base_width);

  float disp_azimuth = azimuth_s;
  float disp_elevation = elevation_s;
  float disp_gain = gain_s;
  float disp_width = width_s;

  for (unsigned long i = 0; i < frames; i++) {
    float azimuth = azimuth_s;
    float elevation = elevation_s;
    float gain = gain_s;
    float width = width_s;

    // Process control inputs
    for (int j = 0; j < m->num_control_inputs; j++) {
      if (!m->control_inputs[j] || !m->control_input_params[j])
        continue;

      const char *param = m->control_input_params[j];
      float control = m->control_inputs[j][i];
      control = fminf(fmaxf(control, -1.0f), 1.0f);

      if (strcmp(param, "azi") == 0) {
        azimuth += control * 180.0f;
      } else if (strcmp(param, "elev") == 0) {
        elevation += control * 90.0f;
      } else if (strcmp(param, "gain") == 0) {
        gain += control;
      } else if (strcmp(param, "width") == 0) {
        width += control * 0.5f;
      }
    }

    clampf(&azimuth, 0.0f, 360.0f);
    clampf(&elevation, -90.0f, 90.0f);
    clampf(&gain, 0.0f, 2.0f);
    clampf(&width, 0.0f, 1.0f);

    disp_azimuth = azimuth;
    disp_elevation = elevation;
    disp_gain = gain;
    disp_width = width;

    // Convert to radians
    float az_rad = azimuth * M_PI / 180.0f;
    float el_rad = elevation * M_PI / 180.0f;

    // Get input samples
    float w = w_ch ? w_ch[i] : 0.0f;
    float x = x_ch ? x_ch[i] : 0.0f;
    float y = y_ch ? y_ch[i] : 0.0f;
    float z = z_ch ? z_ch[i] : 0.0f;

    float decoded;
    if (channel == CHANNEL_LEFT) {
      decoded =
          w + width * (x * cosf(az_rad) + y * sinf(az_rad) + z * sinf(el_rad));
    } else {
      decoded =
          w + width * (x * cosf(az_rad) - y * sinf(az_rad) + z * sinf(el_rad));
    }
    out[i] = decoded * gain;
  }

  pthread_mutex_lock(&s->lock);
  s->display_azimuth = disp_azimuth;
  s->display_elevation = disp_elevation;
  s->display_gain = disp_gain;
  s->display_width = disp_width;
  pthread_mutex_unlock(&s->lock);
}

static void ambi_decode_draw_ui(Module *m, int y, int x) {
  AmbiDecode *s = (AmbiDecode *)m->state;

  pthread_mutex_lock(&s->lock);
  float azimuth = s->display_azimuth;
  float elevation = s->display_elevation;
  float gain = s->display_gain;
  float width = s->display_width;
  AmbiChannel channel = s->channel;
  pthread_mutex_unlock(&s->lock);

  BLUE();
  mvprintw(y, x, "[AmbiDecode%s:%s] ", (channel == CHANNEL_LEFT) ? "L" : "R",
           m->name);
  CLR();

  LABEL(2, "azi:");
  ORANGE();
  printw(" %d", (int)azimuth);
  CLR();

  LABEL(2, " elev:");
  ORANGE();
  printw(" %d", (int)elevation);
  CLR();

  LABEL(2, " gain:");
  ORANGE();
  printw(" %.2f", gain);
  CLR();

  LABEL(2, " width:");
  ORANGE();
  printw(" %.2f", width);
  CLR();

  YELLOW();
  mvprintw(y + 1, x,
           "Real-time keys: -/= (azi), _/+ (elev), [/] (gain), ;/' (width)");
  mvprintw(y + 2, x,
           "Command mode: :1 [azimuth], :2 [elevation], :3 [gain], :4 [width]");
  BLACK();
}

static void ambi_decode_handle_input(Module *m, int key) {
  AmbiDecode *s = (AmbiDecode *)m->state;
  int handled = 0;

  pthread_mutex_lock(&s->lock);

  if (!s->entering_command) {
    switch (key) {
    case '-':
      s->azimuth -= 1.0f;
      handled = 1;
      break;
    case '=':
      s->azimuth += 1.0f;
      handled = 1;
      break;
    case '_':
      s->elevation -= 1.0f;
      handled = 1;
      break;
    case '+':
      s->elevation += 1.0f;
      handled = 1;
      break;
    case '[':
      s->gain -= 0.01f;
      handled = 1;
      break;
    case ']':
      s->gain += 0.01f;
      handled = 1;
      break;
    case ';':
      s->width -= 0.01f;
      handled = 1;
      break;
    case '\'':
      s->width += 0.01f;
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
      if (sscanf(s->command_buffer, "%c %f", &type, &val) == 2) {
        if (type == '1')
          s->azimuth = val;
        else if (type == '2')
          s->elevation = val;
        else if (type == '3')
          s->gain = val;
        else if (type == '4')
          s->width = val;
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

static void ambi_decode_set_osc_param(Module *m, const char *param,
                                      float value) {
  AmbiDecode *s = (AmbiDecode *)m->state;
  pthread_mutex_lock(&s->lock);

  if (strcmp(param, "azi") == 0) {
    s->azimuth = value * 360.0f;
  } else if (strcmp(param, "elev") == 0) {
    s->elevation = (value - 0.5f) * 180.0f;
  } else if (strcmp(param, "gain") == 0) {
    s->gain = value * 1.0f;
  } else if (strcmp(param, "width") == 0) {
    s->width = value;
  } else {
    fprintf(stderr, "[ambi_decode] Unknown OSC param: %s\n", param);
  }

  clamp_params(s);
  pthread_mutex_unlock(&s->lock);
}

static void ambi_decode_destroy(Module *m) {
  AmbiDecode *s = (AmbiDecode *)m->state;
  if (s)
    pthread_mutex_destroy(&s->lock);
  destroy_base_module(m);
}

Module *create_module(const char *args, float sample_rate) {
  float azimuth = 0.0f;
  float elevation = 0.0f;
  float gain = 1.0f;
  float width = 1.0f;
  AmbiChannel channel = CHANNEL_LEFT; // Default to left

  if (args && strstr(args, "azi=")) {
    sscanf(strstr(args, "azi="), "azi=%f", &azimuth);
  }
  if (args && strstr(args, "elev=")) {
    sscanf(strstr(args, "elev="), "elev=%f", &elevation);
  }
  if (args && strstr(args, "gain=")) {
    sscanf(strstr(args, "gain="), "gain=%f", &gain);
  }
  if (args && strstr(args, "width=")) {
    sscanf(strstr(args, "width="), "width=%f", &width);
  }
  if (args && strstr(args, "ch=")) {
    const char *ch_str = strstr(args, "ch=") + 3;
    if (strncmp(ch_str, "left", 4) == 0) {
      channel = CHANNEL_LEFT;
    } else if (strncmp(ch_str, "right", 5) == 0) {
      channel = CHANNEL_RIGHT;
    }
  }

  AmbiDecode *s = calloc(1, sizeof(AmbiDecode));
  s->azimuth = azimuth;
  s->elevation = elevation;
  s->gain = gain;
  s->width = width;
  s->channel = channel;
  s->sample_rate = sample_rate;

  pthread_mutex_init(&s->lock, NULL);
  init_smoother(&s->smooth_azimuth, 0.75f);
  init_smoother(&s->smooth_elevation, 0.75f);
  init_smoother(&s->smooth_gain, 0.75f);
  init_smoother(&s->smooth_width, 0.75f);
  clamp_params(s);

  s->display_azimuth = s->azimuth;
  s->display_elevation = s->elevation;
  s->display_gain = s->gain;
  s->display_width = s->width;

  Module *m = calloc(1, sizeof(Module));
  m->name = "ambi_decode";
  m->state = s;

  m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));
  m->process = ambi_decode_process;
  m->draw_ui = ambi_decode_draw_ui;
  m->handle_input = ambi_decode_handle_input;
  m->set_param = ambi_decode_set_osc_param;
  m->destroy = ambi_decode_destroy;

  return m;
}
