// engine.c
#include <portaudio.h>
#include <string.h>
#include "engine.h"

static int audio_callback(const void* input, void* output,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void* userData) {
    Patch* patch = (Patch*)userData;
    float* out = (float*)output;

    // Process every module
    for (int i = 0; i < patch->module_count; ++i) {
        Module* m = patch->modules[i];
        float* in = NULL;
        if (m->connect_input) {
            // Assume connected inputs were set externally (e.g., patch.modules[1]->connect_input)
        }
        m->process(m, in, m->output, framesPerBuffer);
    }

    // Output final module's output (for now, hardcoded)
    memcpy(out, patch->modules[1]->output, sizeof(float) * framesPerBuffer);
    return paContinue;
}

int start_audio(Patch* patch) {
    Pa_Initialize();
    PaStream* stream;
    Pa_OpenDefaultStream(&stream, 0, 1, paFloat32,
                         patch->sample_rate, patch->frame_count,
                         audio_callback, patch);
    Pa_StartStream(stream);
    return 0;
}

