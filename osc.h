#ifndef OSC_H
#define OSC_H
#include <lo/lo.h>

// void start_osc_server(void);
lo_server_thread start_osc_server(void);
const char* get_current_osc_port(void);

#endif

