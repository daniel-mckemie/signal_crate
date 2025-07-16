#include <stdio.h>
#include <string.h>
#include <lo/lo.h>
#include "engine.h"  // for get_module_count()
#include "module.h"  // for Module struct

// Optional error handler for liblo
void osc_error_handler(int num, const char *msg, const char *path) {
    fprintf(stderr, "[osc] liblo error %d in path %s: %s\n", num, path ? path : "(none)", msg);
}

// Generic handler for all control messages
static int generic_handler(const char *path, const char *types,
                           lo_arg **argv, int argc, lo_message msg, void *user_data) {
    if (get_module_count() == 0) return 1;

    Module* m = get_module(0);
    if (!m || !m->set_param) return 1;

    float value = 0.0f;
    if (argc > 0) {
        if (types[0] == 'f') {
            value = argv[0]->f;
        } else if (types[0] == 'i') {
            value = (float)argv[0]->i;
        }
    }

	printf("[osc] Received %s = %f\n", path, argv[0]->f);
    if (strcmp(path, "/slider1") == 0) {
        m->set_param(m, "freq", value);
    } else if (strcmp(path, "/slider2") == 0) {
        m->set_param(m, "amp", value);
    } else if (strcmp(path, "/button1") == 0) {
        m->set_param(m, "waveform_next", value);
    }

    return 0;
}

lo_server_thread start_osc_server(void) {
    const int base_port = 61234;
    const int max_attempts = 10;
    lo_server_thread st = NULL;
    char port_str[16];

    for (int i = 0; i < max_attempts; ++i) {
        snprintf(port_str, sizeof(port_str), "%d", base_port + i);
        st = lo_server_thread_new(port_str, osc_error_handler);
        if (st) {
			lo_server_thread_add_method(st, NULL, NULL, generic_handler, NULL);
			lo_server_thread_start(st);
            printf("[osc] OSC server started on port %s\n", port_str);
            return st;
        }
    }

    fprintf(stderr, "[osc] Failed to bind any port from %d to %d\n", base_port, base_port + max_attempts - 1);
    return NULL;
}
