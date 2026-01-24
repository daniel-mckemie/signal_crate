#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include <sndfile.h>

#include "e_normalize.h"
#include "module.h"
#include "util.h"

#define E_FILES_DIR "e_output_files"
#define NORM_DIR "e_output_files/normalize"
#define BLOCK_FRAMES 4096

static void ensure_norm_dir(void) {
	mkdir(E_FILES_DIR, 0755);
    mkdir(NORM_DIR, 0755);
}

static void normalize_file(const char* filepath)
{
    SF_INFO info = (SF_INFO){0};
    SNDFILE* in = sf_open(filepath, SFM_READ, &info);
    if (!in) {
        fprintf(stderr, "[e_normalize] failed to open '%s'\n", filepath);
        return;
    }

    float* buf = malloc(sizeof(float) * BLOCK_FRAMES * info.channels);
    float peak = 0.0f;

    sf_count_t frames;
    while ((frames = sf_readf_float(in, buf, BLOCK_FRAMES)) > 0) {
        for (sf_count_t i = 0; i < frames * info.channels; i++) {
            float a = fabsf(buf[i]);
            if (a > peak) peak = a;
        }
    }

    if (peak <= 0.0f) {
        sf_close(in);
        free(buf);
        return;
    }

    float gain = 1.0f / peak;
    sf_seek(in, 0, SEEK_SET);

	const char* fname = strrchr(filepath, '/');
	fname = fname ? fname + 1 : filepath;  // strip path if present

	char stem[512];
	strncpy(stem, fname, sizeof(stem));
	stem[sizeof(stem) - 1] = '\0';

	char* dot = strrchr(stem, '.');
	if (dot) *dot = '\0';

	char outpath[1024];
	snprintf(outpath, sizeof(outpath),
			 NORM_DIR "/%s_norm.wav", stem);


    SNDFILE* out = sf_open(outpath, SFM_WRITE, &info);
    if (!out) {
        sf_close(in);
        free(buf);
        return;
    }

    while ((frames = sf_readf_float(in, buf, BLOCK_FRAMES)) > 0) {
        for (sf_count_t i = 0; i < frames * info.channels; i++) {
            buf[i] *= gain;
        }
        sf_writef_float(out, buf, frames);
    }

    sf_close(in);
    sf_close(out);
    free(buf);
}


static void normalize_process(Module* m, float* in, unsigned long frames)
{
    (void)in;
    memset(m->output_buffer, 0, sizeof(float) * frames);
}

static void normalize_destroy(Module* m)
{
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate)
{
    (void)sample_rate;

    char filepath[512] = "";

    if (args && strstr(args, "file=")) {
        const char* p = strstr(args, "file=") + 5;
        size_t i = 0;
        while (*p && *p != ',' && *p != ' ' && i < sizeof(filepath) - 1) {
            filepath[i++] = *p++;
        }
        filepath[i] = '\0';
    }

    if (filepath[0] == '\0') {
        fprintf(stderr, "[e_normalize] missing file=\n");
        return NULL;
    }

    ensure_norm_dir();
    normalize_file(filepath);

    ENormalize* s = calloc(1, sizeof(ENormalize));
    s->executed = 1;

    Module* m = calloc(1, sizeof(Module));
    m->name = "e_normalize";
    m->state = s;
    m->process = normalize_process;
    m->destroy = normalize_destroy;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));

    return m;
}

