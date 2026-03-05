#ifndef AMBI_DECODE_H
#define AMBI_DECODE_H

#include <pthread.h>
#include <stdbool.h>
#include "util.h"

typedef enum {
    CHANNEL_LEFT,
    CHANNEL_RIGHT
} AmbiChannel;

typedef struct {
    float sample_rate;

    // Ambisonic decoding parameters
    float azimuth;      // Listener rotation in degrees (0-360)
    float elevation;    // Vertical angle in degrees (-90 to +90)
    float gain;         // Output gain
    float width;        // Stereo width (0=mono, 1=full width)

    // Channel selection
    AmbiChannel channel;

    // Display parameters
    float display_azimuth;
    float display_elevation;
    float display_gain;
    float display_width;

    // Parameter smoothers
    CParamSmooth smooth_azimuth;
    CParamSmooth smooth_elevation;
    CParamSmooth smooth_gain;
    CParamSmooth smooth_width;

    // UI state
    bool entering_command;
    char command_buffer[64];
    int command_index;

    pthread_mutex_t lock;
} AmbiDecode;

#endif

