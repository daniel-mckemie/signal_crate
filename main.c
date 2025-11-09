#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <portaudio.h>
#include <ncurses.h>
#include <signal.h>

#include "engine.h"
#include "ui.h"
#include "util.h"
#include "osc.h"

float sample_rate = 48000.0f;

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

    process_audio(in, out, framesPerBuffer);

    if (allocated_input) free(in);
    return paContinue;
}

void handle_signal(int sig) {
    endwin();  // restore terminal if UI is active
    fprintf(stderr, "\n[main] Caught signal %d — clean exit.\n", sig);
    exit(1);
}

int main(int argc, char** argv) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGSEGV, handle_signal);

    Pa_Initialize();
    PaStream* stream;

    // Get default devices
    PaDeviceIndex outputDevice = Pa_GetDefaultOutputDevice();
    PaDeviceIndex inputDevice = Pa_GetDefaultInputDevice();

    if (outputDevice == paNoDevice) {
        fprintf(stderr, "No default output device.\n");
        Pa_Terminate();
        return 1;
    }

    const PaDeviceInfo* outputInfo = Pa_GetDeviceInfo(outputDevice);
    const PaDeviceInfo* inputInfo  = (inputDevice != paNoDevice)
                                    ? Pa_GetDeviceInfo(inputDevice)
                                    : NULL;
    sample_rate = outputInfo->defaultSampleRate;

    // --- SAFE PATCH LOADER ---
    char* patch = NULL;
    if (argc > 1) {
        FILE* f = fopen(argv[1], "r");
        if (!f) {
            fprintf(stderr, "[main] Failed to open patch file: %s\n", argv[1]);
            Pa_Terminate();
            return 1;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        rewind(f);

        patch = (char*)calloc((size_t)size + 1, 1);
        if (!patch) {
            fclose(f);
            fprintf(stderr, "[main] Out of memory.\n");
            Pa_Terminate();
            return 1;
        }

        fread(patch, 1, (size_t)size, f);
        fclose(f);
    } else {
        printf("Enter patch (end with an empty line):\n");
        size_t cap = 8192, len = 0;
        patch = (char*)malloc(cap);
        if (!patch) {
            fprintf(stderr, "[main] Out of memory.\n");
            Pa_Terminate();
            return 1;
        }
        patch[0] = '\0';

        char line[1024];
        while (fgets(line, sizeof(line), stdin)) {
            if (strcmp(line, "\n") == 0) break;
            size_t L = strlen(line);
            if (len + L + 1 > cap) {
                cap *= 2;
                char* np = realloc(patch, cap);
                if (!np) {
                    free(patch);
                    fprintf(stderr, "[main] Out of memory (stdin read).\n");
                    Pa_Terminate();
                    return 1;
                }
                patch = np;
            }
            memcpy(patch + len, line, L);
            len += L;
            patch[len] = '\0';
        }
    }

    // --- Initialize engine ---
    initialize_engine(patch);
    free(patch);

    // Safety: ensure we have at least one module producing audio
    bool has_audio = false;
    for (int i = 0; i < get_module_count(); i++) {
        Module* m = get_module(i);
        if (m && m->output_buffer) {
            has_audio = true;
            break;
        }
    }

    if (!has_audio) {
        fprintf(stderr, "[error] No modules with audio output found. Exiting.\n");
        Pa_Terminate();
        return 1;
    }

    // Start OSC server if any modules exist
    if (get_module_count() > 0) {
        start_osc_server();
    } else {
        fprintf(stderr, "[warn] No modules loaded. Skipping OSC server.\n");
    }

    // --- Configure PortAudio ---
    PaStreamParameters inputParams = {
        .device = inputDevice,
        .channelCount = 1,
        .sampleFormat = paFloat32,
        .suggestedLatency = inputInfo ? inputInfo->defaultLowInputLatency : 0.01,
        .hostApiSpecificStreamInfo = NULL
    };

    PaStreamParameters outputParams = {
        .device = outputDevice,
        .channelCount = outputInfo->maxOutputChannels,
        .sampleFormat = paFloat32,
        .suggestedLatency = outputInfo->defaultLowOutputLatency,
        .hostApiSpecificStreamInfo = NULL
    };

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
        Pa_Terminate();
        return 1;
    }
	
	extern int g_num_output_channels;
	g_num_output_channels = outputParams.channelCount;
	fprintf(stderr, "[main] using %d output channels\n", g_num_output_channels);

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "Failed to start stream: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    const PaStreamInfo* info = Pa_GetStreamInfo(stream);
    if (info) {
        sample_rate = info->sampleRate;
        printf("Actual stream sample rate: %.2f Hz\n", sample_rate);
    }

    // --- Run UI (blocking) ---
    ui_loop();

    // --- Cleanup ---
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    shutdown_engine();
    printf("Clean exit.\n");
    return EXIT_SUCCESS;
}

