#include <stdio.h>
#include <portaudio.h>
#include <pthread.h>
#include "audio.h"
#include "ui.h"

int main() {
	Pa_Initialize();

	// Gets the in/out default device in Audio/MIDI Setup
	int defaultDevice = Pa_GetDefaultOutputDevice();
	if (defaultDevice == paNoDevice) {
		fprintf(stderr, "No default output device.\n");
		return 1;
	}

	const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(defaultDevice);
	double sampleRate = deviceInfo->defaultSampleRate;

	printf("Using sample rate: %.2f Hz\n", sampleRate);

	PaStream *stream;
	// Don't need smoother functions in state here
	MoogFilter state = {
		.cutoff = 440.f,
		.resonance = 0.00f,
		.sample_rate = (float)sampleRate,
		.running = 1 // Indicates running to cleanly quit
	};
	pthread_mutex_init(&state.lock, NULL); // Initialize threading
	
	init_smoother(&state.smooth_cutoff, 0.75f);
	init_smoother(&state.smooth_resonance, 0.75f);

	Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, 
			state.sample_rate, // Sample rate grabbed dynamically from Audio/MIDI Setup
			256, // Buffer size
			audio_callback,
			&state);
	Pa_StartStream(stream); // Start audio

	pthread_t ui_tid; // Init ui thread variable
	pthread_create(&ui_tid, NULL, ui_thread, &state); // Create ui thread
	
	while(state.running) Pa_Sleep(100);

	// Cleanup when quitting
	Pa_StopStream(stream);
	Pa_CloseStream(stream);
	Pa_Terminate();
	pthread_mutex_destroy(&state.lock);
	pthread_join(ui_tid, NULL); // Wait for UI to exit
	return 0;
}

