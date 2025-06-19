#include <stdio.h>
#include <portaudio.h>
#include <pthread.h>
#include "audio.h"
#include "ui.h"

int main() {
  Pa_Initialize();

  int defaultDevice = Pa_GetDefaultOutputDevice();
  if (defaultDevice == paNoDevice) {
    fprintf(stderr, "No defaukt output device.\n");
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
    .sample_rate = (float)sampleRate
  };
  pthread_mutex_init(&state.lock, NULL);

  Pa_OpenDefaultStream(&stream, 1, 1, paFloat32, state.sample_rate, 256,
                      audio_callback, &state);
  Pa_StartStream(stream);

  pthread_t ui_tid;
  pthread_create(&ui_tid, NULL, ui_thread, &state);

  while(1) Pa_Sleep(1000);

  // Not reached
  Pa_CloseStream(stream);
  Pa_Terminate();
  pthread_mutex_destroy(&state.lock);
  return 0;
}
