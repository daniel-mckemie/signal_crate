#ifndef BIT_SPLITTER_H
#define BIT_SPLITTER_H

#include "util.h"
#include <pthread.h>

// bit_splitter — audio module
// Extracts a single bit from an 8-bit quantized audio/CV signal.
// Output is 0.0 or depth (default 1.0) at audio rate.
//
// Usage — instantiate once per bit:
//   bit_splitter([bit=0], v1) as bit0   <- LSB
//   bit_splitter([bit=7], v1) as bit7   <- MSB
//
// Then route as audio CV to amp=, cutoff=, freq= etc.
//
// Input range: -1.0 to 1.0 (bipolar: -1->0x00, 0->0x80, 1->0xFF)
// Parameters:
//   bit    — which bit to extract, 0 (LSB) to 7 (MSB), constructor only
//   thresh — input scale ceiling, default 1.0
//   depth  — output level when bit is high, default 1.0

typedef struct {
    int bit;
    float thresh;

    float sample_rate;

    CParamSmooth smooth_thresh;

    pthread_mutex_t lock;
    bool entering_command;
    char command_buffer[64];
    int command_index;
} BitSplitterState;

#endif
