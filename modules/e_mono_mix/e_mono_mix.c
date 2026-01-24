#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>
#include <sndfile.h>

#include "e_mono_mix.h"
#include "module.h"
#include "util.h"

#define E_FILES_DIR "e_output_files"
#define MONO_DIR    "e_output_files/mono_mix"
#define BLOCK_FRAMES 4096

static void ensure_mono_dir(void) {
    mkdir(E_FILES_DIR, 0755);
    mkdir(MONO_DIR, 0755);
}

static void mono_file(const char* filepath)
{
    SF_INFO info = (SF_INFO){0};
    SNDFILE* in = sf_open(filepath, SFM_READ, &info);
    if (!in) {
        fprintf(stderr, "[e_mono] failed to open '%s'\n", filepath);
        return;
    }

    int channels = info.channels;
    if (channels < 1) {
        sf_close(in);
        return;
    }

    float* inter = malloc(sizeof(float) * BLOCK_FRAMES * channels);
    float* mono  = malloc(sizeof(float) * BLOCK_FRAMES);

    SF_INFO out_info = info;
    out_info.channels = 1;

    const char* fname = strrchr(filepath, '/');
    fname = fname ? fname + 1 : filepath;

    char stem[512];
    strncpy(stem, fname, sizeof(stem));
    stem[sizeof(stem) - 1] = '\0';

    char* dot = strrchr(stem, '.');
    if (dot) *dot = '\0';

    char outpath[1024];
    snprintf(outpath, sizeof(outpath),
             MONO_DIR "/%s_mono.wav", stem);

    SNDFILE* out = sf_open(outpath, SFM_WRITE, &out_info);
    if (!out) {
        sf_close(in);
        free(inter);
        free(mono);
        return;
    }

    sf_count_t frames;
    while ((frames = sf_readf_float(in, inter, BLOCK_FRAMES)) > 0) {
        for (sf_count_t i = 0; i < frames; i++) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ch++) {
                sum += inter[i * channels + ch];
            }
            mono[i] = sum / (float)channels;
        }
        sf_writef_float(out, mono, frames);
    }

    sf_close(in);
    sf_close(out);
    free(inter);
    free(mono);
}

static void mono_process(Module* m, float* in, unsigned long frames)
{
    (void)in;
    memset(m->output_buffer, 0, sizeof(float) * frames);
}

static void mono_destroy(Module* m)
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
        fprintf(stderr, "[e_mono] missing file=\n");
        return NULL;
    }

    ensure_mono_dir();
    mono_file(filepath);

    EMonoMix* s = calloc(1, sizeof(EMonoMix));
    s->executed = 1;

    Module* m = calloc(1, sizeof(Module));
    m->name = "e_mono_mix";
    m->state = s;
    m->process = mono_process;
    m->destroy = mono_destroy;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));

    return m;
}

