#ifndef MIDI_H
#define MIDI_H

// Start PortMIDI input.
// If device_substr is non-NULL and non-empty, picks the first input device whose name contains it.
// Otherwise picks the first available input device.
// Returns 0 on success, nonzero on failure.
int midi_start(const char* device_substr);

// Stop PortMIDI + background thread.
void midi_stop(void);

// Latest CC value (channel 1), raw 0..127. Returns 0 if never seen or invalid cc.
int midi_cc_raw(int cc);

// Latest CC value normalized to 0..1.
float midi_cc_norm(int cc);

// Optional: print devices to stderr (for now).
void midi_print_devices(void);

// For UI monitoring
int midi_last_channel(void);
int midi_last_cc(void);

#endif

