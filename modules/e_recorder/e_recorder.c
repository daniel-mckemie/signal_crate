#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <ncurses.h>
#include <sndfile.h>
#include <sys/stat.h>

#include "e_recorder.h"
#include "module.h"
#include "util.h"

#define INITIAL_SECONDS 1.0
#define E_FILES_DIR "e_output_files"
#define RECORD_DIR "recordings"

static void ensure_record_dir(void) {
	mkdir(E_FILES_DIR, 0755);
    mkdir(RECORD_DIR, 0755);
}

static void grow_buffers(ERecorder* s, uint64_t needed) {
    if (needed <= s->buffer_capacity) return;

    uint64_t newcap = s->buffer_capacity;
    if (newcap == 0) newcap = 1;
    while (newcap < needed) newcap *= 2;

    for (int ch = 0; ch < s->num_inputs; ch++) {
        float* p = realloc(s->buffers[ch], newcap * sizeof(float));
        if (!p) return; // keep old buffers; fail soft
        s->buffers[ch] = p;
    }

    float* mixp = realloc(s->mix_buffer, newcap * sizeof(float));
    if (!mixp) return;
    s->mix_buffer = mixp;

    s->buffer_capacity = newcap;
    s->mix_capacity = newcap;
}

static void ensure_buffers(ERecorder* s, int num_inputs) {
    if (num_inputs <= 0) return;
    if (s->num_inputs == num_inputs &&
        s->buffers && s->buffer_sizes &&
        s->mix_buffer)
        return;

    if (s->buffers) {
        for (int ch = 0; ch < s->num_inputs; ch++) free(s->buffers[ch]);
        free(s->buffers);
        s->buffers = NULL;
    }
    if (s->buffer_sizes) {
        free(s->buffer_sizes);
        s->buffer_sizes = NULL;
    }
    if (s->mix_buffer) {
        free(s->mix_buffer);
        s->mix_buffer = NULL;
    }

    s->num_inputs = num_inputs;
    s->buffers = calloc((size_t)s->num_inputs, sizeof(float*));
    s->buffer_sizes = calloc((size_t)s->num_inputs, sizeof(uint64_t));

    for (int ch = 0; ch < s->num_inputs; ch++) {
        s->buffers[ch] = calloc((size_t)s->buffer_capacity, sizeof(float));
        s->buffer_sizes[ch] = 0;
    }

    s->mix_capacity = s->buffer_capacity;
    s->mix_buffer = calloc((size_t)s->mix_capacity, sizeof(float));
    s->mix_size = 0;
}

static void write_wavs_job(
    float sample_rate,
    unsigned int take_id,
    int num_files,
    float** buffers,
    uint64_t* sizes
) {
    for (int i = 0; i < num_files; i++) {
        char path[1024];

        if (i == num_files - 1) {
            snprintf(path, sizeof(path),
                     RECORD_DIR "/sc_take_%03u_mix.wav",
                     take_id);
        } else {
            snprintf(path, sizeof(path),
                     RECORD_DIR "/sc_take_%03u_ch_%02d.wav",
                     take_id, i);
        }

        SF_INFO info = (SF_INFO){0};
        info.samplerate = (int)sample_rate;
        info.channels = 1;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

        SNDFILE* sf = sf_open(path, SFM_WRITE, &info);
        if (!sf) continue;

        sf_write_float(sf, buffers[i], (sf_count_t)sizes[i]);
        sf_close(sf);
    }
}

static void* writer_main(void* arg) {
    ERecorder* s = (ERecorder*)arg;

    pthread_mutex_lock(&s->writer_lock);
    while (s->writer_running) {
        while (s->writer_running && !s->job_pending) {
            pthread_cond_wait(&s->writer_cv, &s->writer_lock);
        }
        if (!s->writer_running) break;

        unsigned int take_id = s->job_take_id;
        int num_files = s->job_num_inputs;
        float** buffers = s->job_buffers;
        uint64_t* sizes = s->job_sizes;
        float sr = s->sample_rate;

        s->job_pending = false;
        s->job_buffers = NULL;
        s->job_sizes = NULL;
        s->job_num_inputs = 0;

        pthread_mutex_unlock(&s->writer_lock);

        write_wavs_job(sr, take_id, num_files, buffers, sizes);

        for (int i = 0; i < num_files; i++) free(buffers[i]);
        free(buffers);
        free(sizes);

        pthread_mutex_lock(&s->writer_lock);
    }
    pthread_mutex_unlock(&s->writer_lock);
    return NULL;
}

static void multirec_process(Module* m, float* in, unsigned long frames) {
    (void)in;

    ERecorder* s = (ERecorder*)m->state;
    float* out = m->output_buffer;

    float mix_gain = (m->num_inputs > 1) ? (1.0f / (float)m->num_inputs) : 1.0f;

    pthread_mutex_lock(&s->lock);

    ensure_buffers(s, m->num_inputs);

    if (s->state == EREC_RECORDING) {
        uint64_t sc = s->sample_counter;
        uint64_t needed = sc + frames;

        grow_buffers(s, needed);

        for (unsigned long i = 0; i < frames; i++) {
            float sum = 0.0f;

            for (int ch = 0; ch < m->num_inputs; ch++) {
                float v = m->inputs[ch] ? m->inputs[ch][i] : 0.0f;
                if (s->buffers && s->buffers[ch]) s->buffers[ch][sc + i] = v;
                sum += v;
            }

            float mix = sum * mix_gain;
            out[i] = mix;
            if (s->mix_buffer) s->mix_buffer[sc + i] = mix;
        }

        s->sample_counter += frames;

        for (int ch = 0; ch < s->num_inputs; ch++) {
            s->buffer_sizes[ch] = s->sample_counter;
        }
        s->mix_size = s->sample_counter;
        s->display_seconds = (double)s->sample_counter / s->sample_rate;
    } else {
        for (unsigned long i = 0; i < frames; i++) {
            float sum = 0.0f;
            for (int ch = 0; ch < m->num_inputs; ch++) {
                sum += m->inputs[ch] ? m->inputs[ch][i] : 0.0f;
            }
            out[i] = sum * mix_gain;
        }
    }

    pthread_mutex_unlock(&s->lock);
}

static void multirec_draw_ui(Module* m, int y, int x) {
    ERecorder* s = (ERecorder*)m->state;

    pthread_mutex_lock(&s->lock);
    ERecState st = s->state;
    double sec = s->display_seconds;
    unsigned int take = s->take_id;
    pthread_mutex_unlock(&s->lock);

    BLUE();
    mvprintw(y, x, "[e_recorder]");
    CLR();

    LABEL(2, "state:");
    ORANGE(); printw(" %s | ", st == EREC_RECORDING ? "REC" : "IDLE"); CLR();

    LABEL(2, "t:");
    ORANGE(); printw(" %.3f s | ", sec); CLR();

    LABEL(2, "take:");
    ORANGE(); printw(" %03u", take); CLR();

    YELLOW();
    mvprintw(y + 1, x, "SPACE = rec / stop");
    BLACK();
}

static void submit_job_locked(ERecorder* s) {
    uint64_t frames = s->sample_counter;
    if (frames == 0) return;

    int stems = s->num_inputs;
    int files = stems + 1;

    float** jb = calloc((size_t)files, sizeof(float*));
    uint64_t* js = calloc((size_t)files, sizeof(uint64_t));
    if (!jb || !js) {
        free(jb);
        free(js);
        return;
    }

    for (int ch = 0; ch < stems; ch++) {
        jb[ch] = malloc((size_t)frames * sizeof(float));
        if (!jb[ch]) {
            for (int k = 0; k < ch; k++) free(jb[k]);
            free(jb);
            free(js);
            return;
        }
        memcpy(jb[ch], s->buffers[ch], (size_t)frames * sizeof(float));
        js[ch] = frames;
    }

    jb[stems] = malloc((size_t)frames * sizeof(float));
    if (!jb[stems]) {
        for (int k = 0; k < stems; k++) free(jb[k]);
        free(jb);
        free(js);
        return;
    }
    memcpy(jb[stems], s->mix_buffer, (size_t)frames * sizeof(float));
    js[stems] = frames;

    pthread_mutex_lock(&s->writer_lock);

    if (s->job_pending) {
        for (int i = 0; i < s->job_num_inputs; i++) free(s->job_buffers[i]);
        free(s->job_buffers);
        free(s->job_sizes);
    }

    s->job_take_id = s->take_id;
    s->job_num_inputs = files;
    s->job_buffers = jb;
    s->job_sizes = js;
    s->job_pending = true;

    pthread_cond_signal(&s->writer_cv);
    pthread_mutex_unlock(&s->writer_lock);

    s->take_id++;
}

static void multirec_handle_input(Module* m, int key) {
    if (key != ' ') return;

    ERecorder* s = (ERecorder*)m->state;

    pthread_mutex_lock(&s->lock);
    ensure_buffers(s, m->num_inputs);

    if (s->state == EREC_IDLE) {
        s->state = EREC_RECORDING;
        s->sample_counter = 0;
        s->display_seconds = 0.0;
        for (int ch = 0; ch < s->num_inputs; ch++) s->buffer_sizes[ch] = 0;
        s->mix_size = 0;
    } else {
        s->state = EREC_IDLE;

        if (s->sample_counter > 0) submit_job_locked(s);

        s->sample_counter = 0;
        s->display_seconds = 0.0;
        for (int ch = 0; ch < s->num_inputs; ch++) s->buffer_sizes[ch] = 0;
        s->mix_size = 0;
    }

    pthread_mutex_unlock(&s->lock);
}
static void erecorder_set_osc_param(Module* m, const char* param, float value) {
	ERecorder* s = (ERecorder*)m->state;
	pthread_mutex_lock(&s->lock);
	if (strcmp(param, "rec") == 0) {
		if (value >= 0.5f && s->state == EREC_IDLE) {
			s->state = EREC_RECORDING;
			s->sample_counter = 0;
			s->display_seconds = 0.0;
			for (int ch = 0; ch < s->num_inputs; ch++) s->buffer_sizes[ch] = 0;
			s->mix_size = 0;
		} else if (value < 0.5f && s->state == EREC_RECORDING) {
			s->state = EREC_IDLE;
			if (s->sample_counter > 0) submit_job_locked(s);
			s->sample_counter = 0;
			s->display_seconds = 0.0;
			for (int ch = 0; ch < s->num_inputs; ch++) s->buffer_sizes[ch] = 0;
			s->mix_size = 0;
		}
	}

	pthread_mutex_unlock(&s->lock);
}

static void multirec_destroy(Module* m) {
    ERecorder* s = (ERecorder*)m->state;
    if (!s) return;

    pthread_mutex_lock(&s->writer_lock);
    s->writer_running = false;
    pthread_cond_signal(&s->writer_cv);
    pthread_mutex_unlock(&s->writer_lock);
    pthread_join(s->writer_thread, NULL);

    pthread_mutex_lock(&s->writer_lock);
    if (s->job_pending) {
        for (int i = 0; i < s->job_num_inputs; i++) free(s->job_buffers[i]);
        free(s->job_buffers);
        free(s->job_sizes);
    }
    pthread_mutex_unlock(&s->writer_lock);

    pthread_mutex_destroy(&s->writer_lock);
    pthread_cond_destroy(&s->writer_cv);

    if (s->buffers) {
        for (int ch = 0; ch < s->num_inputs; ch++) free(s->buffers[ch]);
        free(s->buffers);
    }
    free(s->buffer_sizes);

    free(s->mix_buffer);

    pthread_mutex_destroy(&s->lock);
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate) {
    (void)args;

    ensure_record_dir();

    ERecorder* s = calloc(1, sizeof(ERecorder));
    s->sample_rate = sample_rate;
    s->state = EREC_IDLE;
    s->take_id = 0;
    s->sample_counter = 0;
    s->display_seconds = 0.0;

    s->buffer_capacity = (uint64_t)(s->sample_rate * INITIAL_SECONDS);

    s->num_inputs = 0;
    s->buffers = NULL;
    s->buffer_sizes = NULL;

    s->mix_capacity = 0;
    s->mix_buffer = NULL;
    s->mix_size = 0;

    pthread_mutex_init(&s->lock, NULL);

    pthread_mutex_init(&s->writer_lock, NULL);
    pthread_cond_init(&s->writer_cv, NULL);
    s->writer_running = true;
    s->job_pending = false;
    s->job_buffers = NULL;
    s->job_sizes = NULL;
    s->job_num_inputs = 0;

    pthread_create(&s->writer_thread, NULL, writer_main, s);

    Module* m = calloc(1, sizeof(Module));
    m->name = "e_recorder";
    m->state = s;
    m->process = multirec_process;
    m->draw_ui = multirec_draw_ui;
    m->handle_input = multirec_handle_input;
	m->set_param = erecorder_set_osc_param;
    m->destroy = multirec_destroy;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));

    return m;
}

