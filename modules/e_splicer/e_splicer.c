#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <ncurses.h>
#include <sndfile.h>
#include <sys/stat.h>
#include <math.h>

#include "e_splicer.h"
#include "util.h"

#define E_AUDIO_DIR   "e_output_files"
#define SPLICE_DIR    "e_output_files/splices"

static void ensure_splice_dir(void) {
    mkdir(E_AUDIO_DIR, 0755);
    mkdir(SPLICE_DIR, 0755);
}

static void set_status(ESplicer* s, const char* msg) {
    strncpy(s->status, msg ? msg : "", sizeof(s->status));
    s->status[sizeof(s->status) - 1] = '\0';
}

static void clamp_params(ESplicer* s) {
    if (s->playback_speed < 0.1f) s->playback_speed = 0.1f;
    if (s->playback_speed > 4.0f) s->playback_speed = 4.0f;

    if (s->frames == 0) { s->playhead = 0; return; }
    if (s->playhead >= s->frames) s->playhead = s->frames - 1;
}

static void normalize_cuts(const ESplicer* s, uint64_t* start, uint64_t* end) {
    uint64_t a = s->cut_a;
    uint64_t b = s->cut_b;
    if (a <= b) { *start = a; *end = b; }
    else { *start = b; *end = a; }
}

static int write_wav_interleaved_double(const char* outpath,
                                        const double* inter,
                                        uint64_t frames,
                                        int channels,
                                        int sr)
{
    SF_INFO info = (SF_INFO){0};
    info.samplerate = sr;
    info.channels   = channels;
    info.format     = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    SNDFILE* out = sf_open(outpath, SFM_WRITE, &info);
    if (!out) return 0;

    const sf_count_t total = (sf_count_t)frames;
    sf_count_t written = 0;
    while (written < total) {
        sf_count_t chunk = total - written;
        if (chunk > 8192) chunk = 8192;
        sf_count_t w = sf_writef_double(out, inter + (size_t)written * (size_t)channels, chunk);
        if (w <= 0) break;
        written += w;
    }

    sf_close(out);
    return written == total;
}

static void derive_stem(const char* filepath, char* stem_out, size_t stem_sz) {
    const char* fname = strrchr(filepath, '/');
    fname = fname ? fname + 1 : filepath;

    strncpy(stem_out, fname, stem_sz);
    stem_out[stem_sz - 1] = '\0';

    char* dot = strrchr(stem_out, '.');
    if (dot) *dot = '\0';
}

static void save_inside(ESplicer* s) {
    if (!s->cut_a_set || !s->cut_b_set) { set_status(s, "need 2 cuts"); return; }

    uint64_t start, end;
    normalize_cuts(s, &start, &end);
    if (end <= start) { set_status(s, "zero"); return; }
    if (end > s->frames) end = s->frames;

    uint64_t frames = end - start;

    uint64_t fade = (uint64_t)(s->file_sr * 0.010); /* 10ms */
    if (fade * 2 > frames) fade = frames / 2;
    if (fade == 0) fade = 1;

    double* out = (double*)calloc((size_t)frames * (size_t)s->channels, sizeof(double));
    if (!out) { set_status(s, "oom"); return; }

    for (uint64_t i = 0; i < frames; i++) {
        double g = 1.0;
        if (i < fade) g = (double)i / (double)fade;
        else if (i >= frames - fade) g = (double)(frames - i) / (double)fade;

        const double* src = s->data + (size_t)(start + i) * (size_t)s->channels;
        double* dst = out + (size_t)i * (size_t)s->channels;

        for (int ch = 0; ch < s->channels; ch++)
            dst[ch] = src[ch] * g;
    }

    char outpath[1024];
    snprintf(outpath, sizeof(outpath),
             "%s/%s_splice_%llu_%llu.wav",
             SPLICE_DIR, s->stem,
             (unsigned long long)start,
             (unsigned long long)end);

    int ok = write_wav_interleaved_double(outpath, out, frames, s->channels, s->file_sr);
    free(out);
    set_status(s, ok ? "saved inside" : "save failed");
}

static void save_outside(ESplicer* s) {
    if (!s->cut_a_set || !s->cut_b_set) { set_status(s, "need 2 cuts"); return; }

    uint64_t start, end;
    normalize_cuts(s, &start, &end);
    if (end <= start) { set_status(s, "zero"); return; }
    if (end > s->frames) end = s->frames;

    uint64_t seg1_frames = start;
    uint64_t seg2_frames = s->frames - end;

    uint64_t fade = (uint64_t)(s->file_sr * 0.020); /* 20ms */
    if (fade < 8) fade = 8;
    if (fade > seg1_frames) fade = seg1_frames;
    if (fade > seg2_frames) fade = seg2_frames;
    if (seg1_frames == 0 || seg2_frames == 0) fade = 0;

    uint64_t out_frames = seg1_frames + seg2_frames;
    if (fade > 0) out_frames -= fade;

    double* out = (double*)calloc((size_t)out_frames * (size_t)s->channels, sizeof(double));
    if (!out) { set_status(s, "oom"); return; }

    uint64_t wpos = 0;
    uint64_t pre1 = (fade > 0) ? (seg1_frames - fade) : seg1_frames;

    if (pre1 > 0) {
        memcpy(out, s->data, (size_t)pre1 * (size_t)s->channels * sizeof(double));
        wpos += pre1;
    }

    if (fade > 0) {
        for (uint64_t i = 0; i < fade; i++) {
            double t = (double)i / (double)(fade - 1);
            double a = cos(t * M_PI * 0.5);
            double b = sin(t * M_PI * 0.5);

            const double* p1 = s->data + (size_t)(pre1 + i) * (size_t)s->channels;
            const double* p2 = s->data + (size_t)(end  + i) * (size_t)s->channels;
            double* dst = out + (size_t)wpos * (size_t)s->channels;

            for (int ch = 0; ch < s->channels; ch++)
                dst[ch] = p1[ch] * a + p2[ch] * b;

            wpos++;
        }
    }

    uint64_t post2_start  = end + fade;
    uint64_t post2_frames = (s->frames > post2_start) ? (s->frames - post2_start) : 0;

    if (post2_frames > 0) {
        memcpy(out + (size_t)wpos * (size_t)s->channels,
               s->data + (size_t)post2_start * (size_t)s->channels,
               (size_t)post2_frames * (size_t)s->channels * sizeof(double));
    }

    char outpath[1024];
    snprintf(outpath, sizeof(outpath),
             "%s/%s_outside_%llu_%llu.wav",
             SPLICE_DIR, s->stem,
             (unsigned long long)start,
             (unsigned long long)end);

    int ok = write_wav_interleaved_double(outpath, out, out_frames, s->channels, s->file_sr);
    free(out);
    set_status(s, ok ? "saved outside" : "save failed");
}

static void save_outside_with_silence(ESplicer* s) {
    if (!s->cut_a_set || !s->cut_b_set) {
        set_status(s, "need 2 cuts");
        return;
    }

    uint64_t start, end;
    normalize_cuts(s, &start, &end);
    if (end <= start) {
        set_status(s, "zero");
        return;
    }
    if (end > s->frames) end = s->frames;

    uint64_t out_frames = s->frames;

    double* out = (double*)malloc((size_t)out_frames *
                                  (size_t)s->channels *
                                  sizeof(double));
    if (!out) {
        set_status(s, "oom");
        return;
    }

    /* copy full file */
    memcpy(out,
           s->data,
           (size_t)out_frames * (size_t)s->channels * sizeof(double));

    /* fade length: 10 ms */
    uint64_t fade = (uint64_t)(s->file_sr * 0.010);
    if (fade < 8) fade = 8;
    if (fade > start) fade = start;
    if (fade > (s->frames - end)) fade = s->frames - end;

    /* fade out BEFORE cut */
    for (uint64_t i = 0; i < fade; i++) {
        double g = 1.0 - ((double)i / (double)fade);
        double* dst = out + (size_t)(start - fade + i) * s->channels;
        for (int ch = 0; ch < s->channels; ch++)
            dst[ch] *= g;
    }

    /* silence region */
    memset(out + (size_t)start * s->channels,
           0,
           (size_t)(end - start) * s->channels * sizeof(double));

    /* fade in AFTER cut */
    for (uint64_t i = 0; i < fade; i++) {
        double g = (double)i / (double)fade;
        const double* src = s->data + (size_t)(end + i) * s->channels;
        double* dst = out + (size_t)(end + i) * s->channels;
        for (int ch = 0; ch < s->channels; ch++)
            dst[ch] = src[ch] * g;
    }

    char outpath[1024];
    snprintf(outpath, sizeof(outpath),
             "%s/%s_outside_silence_%llu_%llu.wav",
             SPLICE_DIR, s->stem,
             (unsigned long long)start,
             (unsigned long long)end);

    int ok = write_wav_interleaved_double(outpath,
                                         out,
                                         out_frames,
                                         s->channels,
                                         s->file_sr);

    free(out);
    set_status(s, ok ? "saved outside+silence" : "save failed");
}

static void splicer_process(Module* m, float* in, unsigned long frames) {
    (void)in;
    ESplicer* s = (ESplicer*)m->state;
    float* out = m->output_buffer;

    pthread_mutex_lock(&s->lock);

    for (unsigned long i = 0; i < frames; i++) {
        float y = 0.0f;

        if (s->playing && s->frames > 0) {
            if (s->playhead < s->frames) {
                const double* f = s->data + (size_t)s->playhead * (size_t)s->channels;
                double sum = 0.0;
                for (int ch = 0; ch < s->channels; ch++) sum += f[ch];
                y = (float)(sum / (double)s->channels);
            }

			s->speed_accum += s->playback_speed;
			uint64_t step = (uint64_t)s->speed_accum;
			s->speed_accum -= step;
			s->playhead += step;

            if (s->playhead >= s->frames) {
                s->playhead = s->frames - 1;
                s->playing = false;
            }
        }

        out[i] = y;
    }

    pthread_mutex_unlock(&s->lock);
}

static void splicer_draw_ui(Module* m, int y, int x) {
    ESplicer* s = (ESplicer*)m->state;

    pthread_mutex_lock(&s->lock);
    uint64_t ph = s->playhead;
    uint64_t frames = s->frames;
    int sr = s->file_sr;
    bool playing = s->playing;
    bool cmd = s->entering_command;
    float spd = s->playback_speed;

    char cmdline[64];
    char status[128];
    strncpy(cmdline, s->command_buffer, sizeof(cmdline));
    cmdline[sizeof(cmdline)-1] = '\0';
    strncpy(status, s->status, sizeof(status));
    status[sizeof(status)-1] = '\0';

    bool a_set = s->cut_a_set;
    bool b_set = s->cut_b_set;
    uint64_t a = s->cut_a;
    uint64_t b = s->cut_b;
    pthread_mutex_unlock(&s->lock);

    double t = (sr > 0) ? ((double)ph / (double)sr) : 0.0;
    double dur = (sr > 0) ? ((double)frames / (double)sr) : 0.0;

    BLUE();
    mvprintw(y, x, "[e_splicer:%s] ", m->name);
    CLR();

    LABEL(2, "");
    ORANGE(); printw(" %.3fs/%.3fs (%s) |", t, dur, playing ? "p" : "s"); CLR();

    LABEL(2, "spd:");
    ORANGE(); printw(" %.2fx | ", spd); CLR();

    LABEL(2, "ph:");
    ORANGE(); printw(" %llu/%llu",
        (unsigned long long)ph,
        (unsigned long long)(frames ? (frames - 1) : 0)); CLR();

    mvprintw(y + 1, x, "cuts: ");
    if (a_set) {
        double ta = (sr > 0) ? ((double)a / (double)sr) : 0.0;
        ORANGE(); printw("A=%llu (%.3fs) ", (unsigned long long)a, ta); CLR();
    } else {
        printw("A=-- ");
    }
    if (b_set) {
        double tb = (sr > 0) ? ((double)b / (double)sr) : 0.0;
        ORANGE(); printw("B=%llu (%.3fs)", (unsigned long long)b, tb); CLR();
    } else {
        printw("B=--");
    }

    YELLOW();
    mvprintw(y + 2, x, "keys: -/= scrub | _/+ fast | [/] speed | SPACE play | c cut");
	mvprintw(y + 3, x, "s save-in | S save-out | x cut-out | R reset | : cmd");
    mvprintw(y + 4, x, "cmd: 1 <sec> | 2 <sample> | 3 <speed>");
    BLACK();

    if (cmd) mvprintw(y + 5, x, ": %s", cmdline);
    else     mvprintw(y + 5, x, "status: %s", status);
}


static void splicer_handle_input(Module* m, int key) {
    ESplicer* s = (ESplicer*)m->state;
    int handled = 0;

    pthread_mutex_lock(&s->lock);

    if (!s->entering_command) {
        uint64_t fine   = (uint64_t)((double)s->file_sr * 0.050);
        uint64_t coarse = (uint64_t)((double)s->file_sr * 0.500);

		switch (key) {
			case '-':
				s->playhead = s->playhead > fine ? s->playhead - fine : 0;
				s->speed_accum = 0.0f;
				handled = 1;
				break;

			case '=':
				s->playhead += fine;
				s->speed_accum = 0.0f;
				handled = 1;
				break;

			case '_':
				s->playhead = s->playhead > coarse ? s->playhead - coarse : 0;
				s->speed_accum = 0.0f;
				handled = 1;
				break;

			case '+':
				s->playhead += coarse;
				s->speed_accum = 0.0f;
				handled = 1;
				break;

			case ' ':
				s->playing = !s->playing;
				if (!s->playing) s->speed_accum = 0.0f;
				handled = 1;
				break;

			case '[':
				s->playback_speed -= 0.01f;
				s->speed_accum = 0.0f;
				handled = 1;
				break;

			case ']':
				s->playback_speed += 0.01f;
				s->speed_accum = 0.0f;
				handled = 1;
				break;

			case 'c':
				if (!s->cut_a_set) {
					s->cut_a = s->playhead;
					s->cut_a_set = true;
				} else if (!s->cut_b_set) {
					s->cut_b = s->playhead;
					s->cut_b_set = true;
				} else {
					s->cut_a = s->cut_b;
					s->cut_b = s->playhead;
				}
				handled = 1;
				break;

			case 's':
				save_inside(s);
				handled = 1;
				break;

			case 'S':
				save_outside(s);
				handled = 1;
				break;

			case 'X':
				save_outside_with_silence(s);
				s->playing = false;
				s->speed_accum = 0.0f;
				clamp_params(s);
				handled = 1;
				break;

			case 'R':
				s->cut_a_set = false;
				s->cut_b_set = false;
				handled = 1;
				break;

            case ':':
                s->entering_command = true;
                s->command_index = 0;
                s->command_buffer[0] = '\0';
                handled = 1;
                break;
        }
	} else {
		if (key == 27) {  // ESC
			s->entering_command = false;
			s->command_index = 0;
			s->command_buffer[0] = '\0';
			handled = 1;
		}
		else if (key == '\n' || key == '\r' || key == KEY_ENTER) {
			char c = 0;
			double v = 0.0;

			if (sscanf(s->command_buffer, " %c %lf", &c, &v) == 2) {
				if (c == '1') {
					if (v < 0.0) v = 0.0;
					s->playhead = (uint64_t)(v * (double)s->file_sr);
					s->speed_accum = 0.0f;
				}
				else if (c == '2') {
					if (v < 0.0) v = 0.0;
					s->playhead = (uint64_t)v;
					s->speed_accum = 0.0f;
				}
				else if (c == '3') {
					s->playback_speed = (float)v;
					s->speed_accum = 0.0f;
					clamp_params(s);
				}
				else {
					set_status(s, "bad cmd");
				}
			}

			s->entering_command = false;
			s->command_index = 0;
			s->command_buffer[0] = '\0';
			handled = 1;
		}
		else if ((key == KEY_BACKSPACE || key == 127) && s->command_index > 0) {
			s->command_index--;
			s->command_buffer[s->command_index] = '\0';
			handled = 1;
		}
		else if (key >= 32 && key < 127 &&
				 s->command_index < (int)sizeof(s->command_buffer) - 1) {
			s->command_buffer[s->command_index++] = (char)key;
			s->command_buffer[s->command_index] = '\0';
			handled = 1;
		}
	}

    if (handled) clamp_params(s);
    pthread_mutex_unlock(&s->lock);
}

static void splicer_destroy(Module* m) {
    ESplicer* s = (ESplicer*)m->state;
    pthread_mutex_destroy(&s->lock);
    free(s->data);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    char filepath[512] = "";

    if (args && strstr(args, "file=")) {
        const char* p = strstr(args, "file=") + 5;
        size_t i = 0;
        while (*p && *p != ',' && *p != ' ' && i < sizeof(filepath) - 1)
            filepath[i++] = *p++;
        filepath[i] = '\0';
    }

    if (!filepath[0]) {
        fprintf(stderr, "[e_splicer] missing file=\n");
        return NULL;
    }

	SF_INFO info = (SF_INFO){0};
	SNDFILE* in = sf_open(filepath, SFM_READ, &info);
	if (!in) {
		fprintf(stderr,
				"[e_splicer] failed to open wav file '%s' or file is not mono.\n",
				filepath);
		if (in) sf_close(in);
		exit(1);
	}


    if (info.frames <= 0 || info.channels <= 0 || info.samplerate <= 0) {
        fprintf(stderr, "[e_splicer] bad file\n");
        sf_close(in);
        return NULL;
    }

    if (info.samplerate != (int)sample_rate) {
        fprintf(stderr, "[e_splicer] sample-rate mismatch: engine=%.0f file=%d\n",
                sample_rate, info.samplerate);
        sf_close(in);
        return NULL;
    }

    double* data = (double*)malloc((size_t)info.frames * (size_t)info.channels * sizeof(double));
    if (!data) {
        fprintf(stderr, "[e_splicer] out of memory\n");
        sf_close(in);
        return NULL;
    }

    sf_count_t got = sf_readf_double(in, data, info.frames);
    sf_close(in);
    if (got != info.frames) {
        fprintf(stderr, "[e_splicer] read failed\n");
        free(data);
        return NULL;
    }

    ensure_splice_dir();

    ESplicer* s = (ESplicer*)calloc(1, sizeof(ESplicer));
    pthread_mutex_init(&s->lock, NULL);

    s->data     = data;
    s->frames   = (uint64_t)info.frames;
    s->channels = info.channels;
    s->file_sr  = info.samplerate;
    s->format   = info.format;

    s->playhead = 0;
    s->playing  = false;
	s->playback_speed = 1.0f;
	s->speed_accum = 0.0f;

    s->cut_a_set = false;
    s->cut_b_set = false;

    s->entering_command = false;
    s->command_index = 0;
    s->command_buffer[0] = '\0';

    strncpy(s->src_path, filepath, sizeof(s->src_path));
    s->src_path[sizeof(s->src_path) - 1] = '\0';
    derive_stem(filepath, s->stem, sizeof(s->stem));

    set_status(s, "ready");

    Module* m = (Module*)calloc(1, sizeof(Module));
    m->name = "e_splicer";
    m->state = s;
    m->output_buffer = (float*)calloc(MAX_BLOCK_SIZE, sizeof(float));
    m->process = splicer_process;
    m->draw_ui = splicer_draw_ui;
    m->handle_input = splicer_handle_input;
    m->destroy = splicer_destroy;

    return m;
}

