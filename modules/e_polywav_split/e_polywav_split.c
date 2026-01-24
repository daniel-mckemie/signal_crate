#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "e_polywav_split.h"
#include "module.h"
#include "util.h"

#define BLOCK_FRAMES 4096
#define E_FILES_DIR "e_output_files"
#define SPLIT_DIR "polywav_splits"

static void ensure_split_dir(void) {
    mkdir(SPLIT_DIR, 0755);
}

static void split_polywav(const char* filepath, unsigned int split_id)
{
    SF_INFO in_info = (SF_INFO){0};
    SNDFILE* infile = sf_open(filepath, SFM_READ, &in_info);
    if (!infile) {
        fprintf(stderr, "[e_polywav_splitter] failed to open '%s'\n", filepath);
        return;
    }

    int channels = in_info.channels;

    SF_INFO out_info = in_info;
    out_info.channels = 1;

    SNDFILE** outs = calloc((size_t)channels, sizeof(SNDFILE*));
    if (!outs) {
        sf_close(infile);
        return;
    }

    for (int ch = 0; ch < channels; ch++) {
        char path[1024];
        snprintf(path, sizeof(path),
                 SPLIT_DIR "/sc_split_%03u_ch_%02d.wav",
                 split_id, ch);

        outs[ch] = sf_open(path, SFM_WRITE, &out_info);
        if (!outs[ch]) {
            fprintf(stderr, "[e_polywav_splitter] failed output ch %d\n", ch);
            for (int k = 0; k < channels; k++) if (outs[k]) sf_close(outs[k]);
            free(outs);
            sf_close(infile);
            return;
        }
    }

    float* inter = malloc(sizeof(float) * BLOCK_FRAMES * (size_t)channels);
    float* mono  = malloc(sizeof(float) * BLOCK_FRAMES);
    if (!inter || !mono) {
        for (int k = 0; k < channels; k++) if (outs[k]) sf_close(outs[k]);
        free(outs);
        sf_close(infile);
        free(inter);
        free(mono);
        return;
    }

    sf_count_t frames;
    while ((frames = sf_readf_float(infile, inter, BLOCK_FRAMES)) > 0) {
        for (int ch = 0; ch < channels; ch++) {
            for (sf_count_t i = 0; i < frames; i++) {
                mono[i] = inter[i * channels + ch];
            }
            sf_writef_float(outs[ch], mono, frames);
        }
    }

    for (int ch = 0; ch < channels; ch++) sf_close(outs[ch]);
    sf_close(infile);

    free(outs);
    free(inter);
    free(mono);
}

static void splitter_process(Module* m, float* in, unsigned long frames)
{
    (void)in;
    memset(m->output_buffer, 0, sizeof(float) * frames);
}

static void splitter_destroy(Module* m)
{
    destroy_base_module(m);
}

Module* create_module(const char* args, float sample_rate)
{
    (void)sample_rate;

    char filepath[512] = "polywav.wav";
    unsigned int split_id = 0;

    if (args && strstr(args, "file=")) {
        const char* p = strstr(args, "file=") + 5;
        size_t i = 0;
        while (*p && *p != ',' && *p != ' ' && i < sizeof(filepath) - 1) {
            filepath[i++] = *p++;
        }
        filepath[i] = '\0';
    }

    if (args && strstr(args, "id=")) {
        sscanf(strstr(args, "id="), "id=%u", &split_id);
    }

    ensure_split_dir();
    split_polywav(filepath, split_id);

    EPolywavSplit* s = calloc(1, sizeof(EPolywavSplit));

    Module* m = calloc(1, sizeof(Module));
    m->name = "e_polywav_splitter";
    m->state = s;
    m->process = splitter_process;
    m->destroy = splitter_destroy;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));

    return m;
}

