#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <portaudio.h>
#include "engine.h"
#include "ui.h"
#include "util.h"
#include "osc.h"

float sample_rate = 44100.0f;

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

    process_audio(in, out, framesPerBuffer);  // NEW DAG ENGINE CALL

    if (allocated_input) free(in);
    return paContinue;
}

int main() {
    Pa_Initialize();
    PaStream* stream;

    // Get default devices
    PaDeviceIndex outputDevice = Pa_GetDefaultOutputDevice();
    PaDeviceIndex inputDevice = Pa_GetDefaultInputDevice();

    if (outputDevice == paNoDevice) {
        fprintf(stderr, "No default output device.\n");
        return 1;
    }

    const PaDeviceInfo* outputInfo = Pa_GetDeviceInfo(outputDevice);
    const PaDeviceInfo* inputInfo  = Pa_GetDeviceInfo(inputDevice);
    sample_rate = outputInfo->defaultSampleRate;

    // Ask user for patch
    char patch[4096] = {0};
    char line[256];
    printf("Enter patch (end with an empty line):\n");

    while (fgets(line, sizeof(line), stdin)) {
        if (strcmp(line, "\n") == 0) break;
        strcat(patch, line);
    }

    initialize_engine(patch);  // NEW DAG PATCH PARSER
	if (get_module_count() > 0) {
		start_osc_server();
	} else {
		fprintf(stderr, "No modules loaded. Skipping OSC server start.");
	}
    // Set up PortAudio stream params
    PaStreamParameters inputParams = {
        .device = inputDevice,
        .channelCount = 1,
        .sampleFormat = paFloat32,
        .suggestedLatency = inputInfo ? inputInfo->defaultLowInputLatency : 0.01,
        .hostApiSpecificStreamInfo = NULL
    };

    PaStreamParameters outputParams = {
        .device = outputDevice,
        .channelCount = 1,
        .sampleFormat = paFloat32,
        .suggestedLatency = outputInfo->defaultLowOutputLatency,
        .hostApiSpecificStreamInfo = NULL
    };

    // Open stream
    PaError err = Pa_OpenStream(&stream,
                                (inputDevice != paNoDevice) ? &inputParams : NULL,
                                &outputParams,
                                sample_rate,
                                FRAMES_PER_BUFFER,
                                paClipOff,
                                audio_callback,
                                NULL);
    if (err != paNoError) {
        fprintf(stderr, "Failed to open stream: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "Failed to start stream: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    const PaStreamInfo* info = Pa_GetStreamInfo(stream);
    if (info) {
        sample_rate = info->sampleRate;
        printf("Actual stream sample rate: %.2f Hz\n", sample_rate);
    } else {
        fprintf(stderr, "Failed to get stream info.\n");
        return 1;
    }

    // Run the ncurses UI loop
    ui_loop();

    // Cleanup
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    shutdown_engine();  // NEW DAG CLEANUP

    printf("Clean exit.\n");
    return EXIT_SUCCESS;
}

