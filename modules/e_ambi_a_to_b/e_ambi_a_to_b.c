#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "e_ambi_a_to_b.h"
#include "module.h"
#include "util.h"

#define BLOCK_FRAMES 4096
#define E_FILES_DIR "e_output_files"
#define A_TO_B_DIR "e_output_files/ambi_a_to_b"

static void ensure_a_to_b_dir(void) {
    mkdir(E_FILES_DIR, 0755);
    mkdir(A_TO_B_DIR, 0755);
}

static void convert_a_to_b(const char *filepath, unsigned int convert_id,
                           int ch[4]) {
    SF_INFO in_info = (SF_INFO){0};
    SNDFILE *infile = sf_open(filepath, SFM_READ, &in_info);
    if (!infile) {
        fprintf(stderr, "[e_ambi_a_to_b] failed to open '%s'\n", filepath);
        return;
    }

    if (in_info.channels < 4) {
        fprintf(stderr,
                "[e_ambi_a_to_b] input must have at least 4 channels, got %d\n",
                in_info.channels);
        sf_close(infile);
        return;
    }

    // Validate channel indices
    for (int i = 0; i < 4; i++) {
        if (ch[i] < 0 || ch[i] >= in_info.channels) {
            fprintf(
                stderr,
                "[e_ambi_a_to_b] channel index %d out of range (file has %d "
                "channels)\n",
                ch[i], in_info.channels);
            sf_close(infile);
            return;
        }
    }

    // Create output file
    char out_path[1024];
    snprintf(out_path, sizeof(out_path), A_TO_B_DIR "/sc_a_to_b_%03u.wav",
             convert_id);

    SF_INFO out_info = in_info;
    out_info.channels = 4; // B-format output is always 4 channels (W, X, Y, Z)

    SNDFILE *outfile = sf_open(out_path, SFM_WRITE, &out_info);
    if (!outfile) {
        fprintf(stderr, "[e_ambi_a_to_b] failed to create output file\n");
        sf_close(infile);
        return;
    }

    float *a_frame = malloc(sizeof(float) * BLOCK_FRAMES * in_info.channels);
    float *b_frame = malloc(sizeof(float) * BLOCK_FRAMES * 4);
    if (!a_frame || !b_frame) {
        sf_close(infile);
        sf_close(outfile);
        free(a_frame);
        free(b_frame);
        return;
    }

    printf("[e_ambi_a_to_b] Converting A-format to B-format: %s -> %s\n",
           filepath, out_path);
    printf("[e_ambi_a_to_b] Using channels: %d, %d, %d, %d (of %d total)\n",
           ch[0], ch[1], ch[2], ch[3], in_info.channels);

    sf_count_t frames;
    while ((frames = sf_readf_float(infile, a_frame, BLOCK_FRAMES)) > 0) {
        for (sf_count_t i = 0; i < frames; i++) {
            // A-format input channels (tetrahedral microphone capsules)
            float lfu = a_frame[i * in_info.channels + ch[0]]; // Left Front Up
            float rfd =
                a_frame[i * in_info.channels + ch[1]]; // Right Front Down
            float lbd = a_frame[i * in_info.channels + ch[2]]; // Left Back Down
            float rbu = a_frame[i * in_info.channels + ch[3]]; // Right Back Up

            // A-to-B conversion: AmbiX / SN3D
            float w = 0.5f * (lfu + rfd + lbd + rbu);
            float x = 0.5f * (lfu + rfd - lbd - rbu);
            float y = 0.5f * (lfu - rfd + lbd - rbu);
            float z = 0.5f * (lfu - rfd - lbd + rbu);

            // AmbiX channel order: W, Y, Z, X
            b_frame[i * 4 + 0] = w;
            b_frame[i * 4 + 1] = y;
            b_frame[i * 4 + 2] = z;
            b_frame[i * 4 + 3] = x;
        }
        sf_writef_float(outfile, b_frame, frames);
    }

    sf_close(infile);
    sf_close(outfile);
    free(a_frame);
    free(b_frame);

    printf("[e_ambi_a_to_b] Conversion complete. B-format file: %s\n",
           out_path);
}

static void a_to_b_process(Module *m, float *in, unsigned long frames) {
    (void)in;
    memset(m->output_buffer, 0, sizeof(float) * frames);
}

static void a_to_b_destroy(Module *m) { destroy_base_module(m); }

Module *create_module(const char *args, float sample_rate) {
    (void)sample_rate;

    char filepath[512] = "ambisonic_a_format.wav";
    unsigned int convert_id = 0;
    int ch[4] = {0, 1, 2, 3}; // Default to first 4 channels

    if (args && strstr(args, "file=")) {
        const char *p = strstr(args, "file=") + 5;
        size_t i = 0;
        while (*p && *p != ',' && *p != ' ' && i < sizeof(filepath) - 1) {
            filepath[i++] = *p++;
        }
        filepath[i] = '\0';
    }

    if (args && strstr(args, "id=")) {
        sscanf(strstr(args, "id="), "id=%u", &convert_id);
    }

    if (args && strstr(args, "channels=")) {
        sscanf(strstr(args, "channels="), "channels=%d,%d,%d,%d", &ch[0],
               &ch[1], &ch[2], &ch[3]);
    }

    ensure_a_to_b_dir();
    convert_a_to_b(filepath, convert_id, ch);

    EAmbiAToB *s = calloc(1, sizeof(EAmbiAToB));

    Module *m = calloc(1, sizeof(Module));
    m->name = "e_ambi_a_to_b";
    m->state = s;
    m->process = a_to_b_process;
    m->destroy = a_to_b_destroy;
    m->output_buffer = calloc(MAX_BLOCK_SIZE, sizeof(float));

    return m;
}
