#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <portaudio.h>
#include "engine.h"
#include "module_loader.h"
#include "ui.h"

#define FRAMES_PER_BUFFER 512

extern float sample_rate;

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

    process_chain(in, out, framesPerBuffer);
    float* buffer = chain[module_count - 1]->output_buffer; // Final modules output 
    memcpy(out, buffer, framesPerBuffer * sizeof(float));
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
    char patch[256];
    printf("Enter patch (e.g., vco, moog_filter, wf_fm_mod, ring_mod, noise_source, amp_mod, looper): ");
    fgets(patch, sizeof(patch), stdin);

    char* token = strtok(patch, " \n");
	while (token && module_count < MAX_MODULES) {
		Module* m = load_module(token, sample_rate);
		if (m) {
			m->output_buffer = calloc(FRAMES_PER_BUFFER, sizeof(float));  // <-- ADD THIS
			chain[module_count++] = m;
		}
		token = strtok(NULL, " \n");
	}

	// === Connect all non-final modules to the last one ===
	// This assumes you want all earlier modules to route into the last one (e.g., vco vco moog_filter)
	if (module_count > 1) {
		Module* dst = chain[module_count - 1];
		for (int i = 0; i < module_count - 1; i++) {
			connect(chain[i], dst);
		}
	}


    // Set up PortAudio parameters
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

    // Open the stream
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

    // Get actual running rate
    const PaStreamInfo* info = Pa_GetStreamInfo(stream);
    if (info) {
        sample_rate = info->sampleRate;
        printf("Actual stream sample rate: %.2f Hz\n", sample_rate);
    } else {
        fprintf(stderr, "Failed to get stream info.\n");
        return 1;
    }

    // Run the UI
    ui_loop();

    // Cleanup
    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();

    for (int i = 0; i < module_count; i++) {
        free_module(chain[i]);
    }

    printf("Clean exit.\n");
    return EXIT_SUCCESS;
}

