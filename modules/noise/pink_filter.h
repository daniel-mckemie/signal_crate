#ifndef PINK_FILTER_H
#define PINK_FILTER_H

#include <math.h>
#include <string.h>

typedef struct {
	float b[7]; // filter state 
	float a[6]; // recursive filter coeffs
	float g[7]; // input gain
} PinkFilter;

static inline void pink_filter_init(PinkFilter* pf, float sample_rate) {
    const float a44[6] = {
        0.99886f, 0.99332f, 0.96900f,
        0.86650f, 0.55000f, -0.7616f
    };
    const float g44[7] = {
        0.0555179f, 0.0750759f, 0.1538520f,
        0.3104856f, 0.5329522f, 0.0168980f, 0.115926f
    };

    float fs_ratio = 44100.0f / sample_rate;

    for (int i = 0; i < 6; ++i) {
        pf->a[i] = powf(fabsf(a44[i]), fs_ratio);
        pf->g[i] = g44[i] * (1.0f - pf->a[i]);
        if (a44[i] < 0.0f)
            pf->g[i] *= -1.0f;
    }

    pf->g[6] = g44[6];
    memset(pf->b, 0, sizeof(pf->b));
}

static inline float pink_filter_process(PinkFilter* pf, float white) {
    pf->b[0] = pf->a[0] * pf->b[0] + white * pf->g[0];
    pf->b[1] = pf->a[1] * pf->b[1] + white * pf->g[1];
    pf->b[2] = pf->a[2] * pf->b[2] + white * pf->g[2];
    pf->b[3] = pf->a[3] * pf->b[3] + white * pf->g[3];
    pf->b[4] = pf->a[4] * pf->b[4] + white * pf->g[4];
    pf->b[5] = pf->a[5] * pf->b[5] - white * pf->g[5];
    float pink = pf->b[0] + pf->b[1] + pf->b[2] +
                 pf->b[3] + pf->b[4] + pf->b[5] +
                 pf->b[6] + white * 0.5362f;
    pf->b[6] = white * pf->g[6];
    return pink;
}

#endif
