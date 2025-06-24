#include <stdio.h>
#include <portaudio.h>
#include <pthread.h>
#include "wf_fm_mod.h"
#include "ui.h"

int main() {
	Pa_Initialize();

	int defaultDevice = Pa_GetDefaultOutputDevice();
	if (defaultDevice == paNoDevice) {
		fprintf(stderr, "No default output device.\n");
		return 1;
	}

	const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(defaultDevice);
	double sampleRate = deviceInfo->defaultSampleRate;

	printf("Using sample rate: %.2f Hz\n", sampleRate);

	PaStream *stream;
	FMState state = {
		.modulator_phase = 0.0f,
		.modulator_freq = 3.0f,
		.index = 0.5f,
		.fold_threshold_mod = 0.2f,
		.fold_threshold_car = 0.2f,
		.blend = 0.5f,
		.sample_rate = (float)sampleRate,
		.running = 1
	};
	pthread_mutex_init(&state.lock, NULL);

	// Initilize smoothing filters (smoothing coefficients)
	init_smoother(&state.smooth_freq, 0.75f);
	init_smoother(&state.smooth_index, 0.75f);
	init_smoother(&state.smooth_blend, 0.75f);
	init_smoother(&state.smooth_fold_mod, 0.75f);
	init_smoother(&state.smooth_fold_car, 0.75f);


	Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, state.sample_rate, 256,
			audio_callback, &state);
	Pa_StartStream(stream);

	pthread_t ui_tid;
	pthread_create(&ui_tid, NULL, ui_thread, &state);

	while(state.running) Pa_Sleep(100);

	// Clean exit
	Pa_StopStream(stream);
	Pa_CloseStream(stream);
	Pa_Terminate();
	pthread_mutex_destroy(&state.lock);
	pthread_join(ui_tid, NULL);
	return 0;
}
