#include "midi.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <portmidi.h>

static PmStream* g_in = NULL;
static pthread_t g_thread;
static int g_running = 0;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static int g_cc[128]; // CC 0-127
static int g_cc_msb[32]; // CC 0-31 
static int g_cc_lsb[32]; // CC 32-63 -> index 0-31

static int last_midi_channel = -1;
static int last_midi_cc      = -1;

static int contains_icase(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return 1;
    size_t nh = strlen(hay), nn = strlen(needle);
    for (size_t i = 0; i + nn <= nh; i++) {
        size_t k = 0;
        for (; k < nn; k++) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (k == nn) return 1;
    }
    return 0;
}

void midi_print_devices(void) {
    int n = Pm_CountDevices();
    fprintf(stderr, "[midi] devices: %d\n", n);
    for (int i = 0; i < n; i++) {
        const PmDeviceInfo* info = Pm_GetDeviceInfo(i);
        if (!info) continue;
        fprintf(stderr, "  [%d] %s%s%s\n",
                i,
                info->interf ? info->interf : "",
                info->interf ? " / " : "",
                info->name ? info->name : "");
        fprintf(stderr, "       input=%d output=%d opened=%d\n",
                info->input, info->output, info->opened);
    }
}

static int pick_input_device(const char* device_substr) {
    int n = Pm_CountDevices();
    int best = -1;
    for (int i = 0; i < n; i++) {
        const PmDeviceInfo* info = Pm_GetDeviceInfo(i);
        if (!info || !info->input) continue;

        if (device_substr && *device_substr) {
            if (contains_icase(info->name, device_substr) || contains_icase(info->interf, device_substr)) {
                best = i;
                break;
            }
        } else {
            best = i;
            break;
        }
    }
    return best;
}

static void handle_event(PmEvent ev) {
    int status = Pm_MessageStatus(ev.message) & 0xFF;
    int data1  = Pm_MessageData1(ev.message) & 0x7F;
    int data2  = Pm_MessageData2(ev.message) & 0x7F;

	if ((status & 0xF0) != 0xB0) return;
	int channel = (status & 0x0F) + 1;

    // CC: data1=cc#, data2=value
    if (data1 < 0 || data1 > 127) return;

    pthread_mutex_lock(&g_lock);
    g_cc[data1] = data2;

	if (data1 < 32) {
		g_cc_msb[data1] = data2;
	} else if (data1 < 64) {
		g_cc_lsb[data1 - 32] = data2;
	}

	last_midi_channel = channel;
	last_midi_cc = data1;
    pthread_mutex_unlock(&g_lock);
}

static void* midi_thread_main(void* _) {
    (void)_;
    PmEvent buf[64];

    while (g_running) {
        if (!g_in) {
            usleep(1000);
            continue;
        }

        if (Pm_Poll(g_in) == TRUE) {
            int nread = Pm_Read(g_in, buf, 64);
            if (nread > 0) {
                for (int i = 0; i < nread; i++) handle_event(buf[i]);
            } else if (nread < 0) {
                fprintf(stderr, "[midi] Pm_Read error: %d\n", nread);
                usleep(5000);
            }
        } else {
            usleep(1000);
        }
    }
    return NULL;
}

int midi_start(const char* device_substr) {
    if (g_running) return 0;

	memset(g_cc_msb, 0, sizeof(g_cc_msb));
	memset(g_cc_lsb, 0, sizeof(g_cc_lsb));


    PmError err = Pm_Initialize();
    if (err != pmNoError) {
        fprintf(stderr, "[midi] Pm_Initialize failed: %s\n", Pm_GetErrorText(err));
        return 1;
    }

    int dev = pick_input_device(device_substr);
    if (dev < 0) {
        fprintf(stderr, "[midi] no MIDI input devices found\n");
        Pm_Terminate();
        return 2;
    }

    const PmDeviceInfo* info = Pm_GetDeviceInfo(dev);
    fprintf(stderr, "[midi] opening input device %d: %s\n", dev, info && info->name ? info->name : "(unknown)");

    err = Pm_OpenInput(&g_in, dev, NULL, 1024, NULL, NULL);
    if (err != pmNoError || !g_in) {
        fprintf(stderr, "[midi] Pm_OpenInput failed: %s\n", Pm_GetErrorText(err));
        g_in = NULL;
        Pm_Terminate();
        return 3;
    }

    g_running = 1;
    if (pthread_create(&g_thread, NULL, midi_thread_main, NULL) != 0) {
        fprintf(stderr, "[midi] pthread_create failed\n");
        g_running = 0;
        Pm_Close(g_in);
        g_in = NULL;
        Pm_Terminate();
        return 4;
    }

    return 0;
}

int midi_last_channel(void) {
    pthread_mutex_lock(&g_lock);
    int c = last_midi_channel;
    pthread_mutex_unlock(&g_lock);
    return c;
}

int midi_last_cc(void) {
    pthread_mutex_lock(&g_lock);
    int cc = last_midi_cc;
    pthread_mutex_unlock(&g_lock);
    return cc;
}

void midi_stop(void) {
    if (!g_running) return;

    g_running = 0;
    pthread_join(g_thread, NULL);

    if (g_in) {
        Pm_Close(g_in);
        g_in = NULL;
    }
    Pm_Terminate();
}

int midi_cc_raw(int cc) {
    if (cc < 0 || cc > 127) return 0;
    pthread_mutex_lock(&g_lock);
    int v = g_cc[cc];
    pthread_mutex_unlock(&g_lock);
    if (v < 0) v = 0;
    if (v > 127) v = 127;
    return v;
}

float midi_cc_norm(int cc) {
    return (float)midi_cc_raw(cc) / 127.0f;
}

int midi_cc14_raw(int cc) {
    if (cc < 0 || cc > 31) return 0;
    pthread_mutex_lock(&g_lock);
    int v = (g_cc_msb[cc] << 7) | g_cc_lsb[cc];
    pthread_mutex_unlock(&g_lock);
    return v;  // 0–16383
}

float midi_cc14_norm(int cc) {
    return midi_cc14_raw(cc) / 16383.0f;
}


