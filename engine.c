#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "module_loader.h"
#include "util.h"

static NamedModule modules[MAX_MODULES];
static int module_count = 0;
extern float sample_rate;

static NamedModule* find_module_by_name(const char* name) {
    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, name) == 0)
            return &modules[i];
    }
    return NULL;
}

static void connect_module_inputs(Module* m, char** input_names, int input_count) {
    for (int i = 0; i < input_count && i < MAX_INPUTS; i++) {
        NamedModule* src = find_module_by_name(input_names[i]);
        if (src) {
            m->inputs[i] = src->module->output_buffer;
        } else {
            fprintf(stderr, "Error: unknown input module '%s'\n", input_names[i]);
        }
    }
}

static void parse_patch_line(const char* line) {
    char modtype[64], alias[64] = {0}, input_str[128] = {0};
    char* inputs[8];
    int input_count = 0;

    if (strstr(line, "(")) {
        sscanf(line, "%[^ (](%[^)]) as %s", modtype, input_str, alias);
    } else if (strstr(line, "as")) {
        sscanf(line, "%s as %s", modtype, alias);
    } else {
        sscanf(line, "%s", modtype);
        snprintf(alias, sizeof(alias), "%s%d", modtype, module_count);
    }

	Module* m = load_module(modtype, sample_rate);
    if (!m) {
        fprintf(stderr, "Failed to load module: %s\n", modtype);
        return;
    }

    NamedModule newmod;
    strncpy(newmod.name, alias, sizeof(newmod.name));
    newmod.module = m;
    modules[module_count++] = newmod;

    if (strlen(input_str)) {
        char* token = strtok(input_str, ",");
        while (token && input_count < 8) {
            inputs[input_count++] = strdup(trim_whitespace(token));
            token = strtok(NULL, ",");
        }
        connect_module_inputs(m, inputs, input_count);
        for (int i = 0; i < input_count; i++) free(inputs[i]);
    }
}

void initialize_engine(const char* patch_text) {
    char* patch = strdup(patch_text);
    char* line = strtok(patch, "\n");
    while (line) {
        parse_patch_line(trim_whitespace(line));
        line = strtok(NULL, "\n");
    }
    free(patch);
}

void shutdown_engine(void) {
    for (int i = 0; i < module_count; i++) {
        if (modules[i].module && modules[i].module->destroy)
            modules[i].module->destroy(modules[i].module);
    }
}

void process_audio(float* input, float* output, unsigned long frames) {
    for (int i = 0; i < module_count; i++) {
        modules[i].module->process(modules[i].module, input, frames);
    }

    // Look for explicitly named "out" module
    for (int i = 0; i < module_count; i++) {
        if (strcmp(modules[i].name, "out") == 0) {
            memcpy(output, modules[i].module->output_buffer, sizeof(float) * frames);
            return;
        }
    }

    // Fallback: use last module's output
    if (module_count > 0) {
        memcpy(output, modules[module_count - 1].module->output_buffer, sizeof(float) * frames);
    } else {
        memset(output, 0, sizeof(float) * frames);
    }
}

Module* get_module(int index) {
    if (index < 0 || index >= module_count) return NULL;
    return modules[index].module;
}

int get_module_count(void) {
    return module_count;
}

