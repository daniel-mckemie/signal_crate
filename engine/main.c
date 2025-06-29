#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <portaudio.h>
#include "engine.h"
#include "module_loader.h"
#include "ui.h"

#define FRAMES_PER_BUFFER 512

static int audio_callback(const void* input, void* output,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo* timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void* userData) {
    float* out = (float*)output;
    float* in = (float*)input;
	int allocated_input = 0;
	if (in == NULL) {
		in = calloc(framesPerBuffer, sizeof(float));
		allocated_input = 1;
	}
    float* buffer = calloc(framesPerBuffer, sizeof(float));
    process_chain(in, buffer, framesPerBuffer);
    memcpy(out, buffer, framesPerBuffer * sizeof(float));
    free(buffer);
	if (allocated_input) free(in);
    return paContinue;
}

int main() {
    char patch[256];
    printf("Enter patch (e.g., vco moog_filter wavefolding-fm-mod): ");
    fgets(patch, sizeof(patch), stdin);

    char* token = strtok(patch, " \n");
    while (token && module_count < MAX_MODULES) {
        Module* m = load_module(token, sample_rate);
        if (m) chain[module_count++] = m;
        token = strtok(NULL, " \n");
    }

    Pa_Initialize();
    PaStream* stream;
    Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, sample_rate, FRAMES_PER_BUFFER, audio_callback, NULL);
    Pa_StartStream(stream);

    ui_loop();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
