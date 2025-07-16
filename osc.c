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


static int module_param_handler(const char *path, const char *types,
                                lo_arg **argv, int argc, lo_message msg, void *user_data) {
    if (argc < 1 || (types[0] != 'f' && types[0] != 'i')) return 1;

    // Path format: /<alias>/<param>
    char alias[64], param[64];
    if (sscanf(path, "/%63[^/]/%63s", alias, param) != 2) {
        fprintf(stderr, "[osc] Invalid path: %s\n", path);
        return 1;
    }

    float value = (types[0] == 'f') ? argv[0]->f : (float)argv[0]->i;

    int module_count = get_module_count();
    for (int i = 0; i < module_count; i++) {
        Module* m = get_module(i);
        if (m && strcmp(alias, get_module_alias(i)) == 0 && m->set_param) {
            m->set_param(m, param, value);
            return 0;
        }
    }

    fprintf(stderr, "[osc] No matching module for alias '%s'\n", alias);
    return 1;
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
			// Wildcard match for any /alias/param
            lo_server_thread_add_method(st, NULL, NULL, module_param_handler, NULL);
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

