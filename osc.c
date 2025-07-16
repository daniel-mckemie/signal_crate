#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <lo/lo.h>
#include "engine.h"  // for get_module_count()
#include "module.h"  // for Module struct

static char current_osc_port[16] = "";

// Optional error handler for liblo
void osc_error_handler(int num, const char *msg, const char *path) {
    fprintf(stderr, "[osc] liblo error %d in path %s: %s\n", num, path ? path : "(none)", msg);
}

// Generic handler for all control messages
static int generic_handler(const char *path, const char *types,
                           lo_arg **argv, int argc, lo_message msg, void *user_data) {
    Module* m = get_module(0);
    if (m && m->set_param && argc > 0 && types[0] == 'f') {
        if (strcmp(path, "/slider1") == 0) {
            m->set_param(m, "freq", argv[0]->f);
        } else if (strcmp(path, "/slider2") == 0) {
            m->set_param(m, "amp", argv[0]->f);
        } else if (strcmp(path, "/button1") == 0) {
            m->set_param(m, "waveform_next", argv[0]->f);
        }
    }

    return 0;
}


lo_server_thread start_osc_server(void) {
    const int base_port = 61234;
    const int max_attempts = 100;
    lo_server_thread st = NULL;
    char port_str[16];

    for (int i = 0; i < max_attempts; ++i) {
        snprintf(port_str, sizeof(port_str), "%d", base_port + i);

        st = lo_server_thread_new_with_proto(port_str, LO_UDP, osc_error_handler);
        if (st) {
            lo_server_thread_add_method(st, NULL, NULL, generic_handler, NULL);
            lo_server_thread_start(st);
			// Save current port for UI display
			snprintf(current_osc_port, sizeof(current_osc_port), "%s", port_str);
            printf("[osc] OSC server started on port %s\n", port_str);
            return st;
        }
    }

    fprintf(stderr, "[osc] Failed to bind any port from %d to %d\n", base_port, base_port + max_attempts - 1);
    return NULL;
}

const char* get_current_osc_port(void) {
    return current_osc_port;
}

