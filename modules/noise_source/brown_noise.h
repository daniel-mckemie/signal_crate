#ifndef BROWN_NOISE_H
#define BROWN_NOISE_H

typedef struct {
    float last;
} BrownNoise;

static inline void brown_noise_init(BrownNoise* b) {
    b->last = 0.0f;
}

static inline float brown_noise_process(BrownNoise* b, float white) {
    b->last += 0.02f * white;

    // Clamp to prevent runaway accumulation
    if (b->last > 1.0f) b->last = 1.0f;
    if (b->last < -1.0f) b->last = -1.0f;

    return 3.0f * b->last;  // Optional loudness normalization
}

#endif
