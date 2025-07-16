#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "engine.h"
#include "module_loader.h"
#include "util.h"

static NamedModule modules[MAX_MODULES];
static int module_count = 0;
extern float sample_rate;

typedef struct {
    char modtype[64];
    char alias[64];
    char input_str[128];
} DeferredPatchLine;

static DeferredPatchLine patch_lines[MAX_MODULES];
static int patch_line_count = 0;

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
    m->num_inputs = input_count;
}

static void parse_patch_line(const char* line) {
    char modtype[64] = {0};
    char alias[64] = {0};
    char input_str[128] = {0};

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
    if (module_count >= MAX_MODULES) {
        fprintf(stderr, "Too many modules! MAX_MODULES = %d\n", MAX_MODULES);
        return;
    }
    modules[module_count++] = newmod;

    strncpy(patch_lines[patch_line_count].modtype, modtype, sizeof(modtype));
    strncpy(patch_lines[patch_line_count].alias, alias, sizeof(alias));
    strncpy(patch_lines[patch_line_count].input_str, input_str, sizeof(input_str));
    patch_line_count++;
}

void initialize_engine(const char* patch_text) {
    char* patch = strdup(patch_text);
    char* line = strtok(patch, "\r\n");

    while (line) {
        char* clean_line = trim_whitespace(line);
        if (strlen(clean_line) > 0) {
            parse_patch_line(clean_line);
        }
        line = strtok(NULL, "\r\n");
    }

    // Second pass: connect inputs
    for (int i = 0; i < patch_line_count; i++) {
        NamedModule* nm = find_module_by_name(patch_lines[i].alias);
        if (!nm) continue;

        char* token;
        char input_buf[128];
        strncpy(input_buf, patch_lines[i].input_str, sizeof(input_buf));

        char* inputs[MAX_INPUTS];
        int input_count = 0;
        token = strtok(input_buf, ",");
        while (token && input_count < MAX_INPUTS) {
            inputs[input_count++] = strdup(trim_whitespace(token));
            token = strtok(NULL, ",");
        }
        connect_module_inputs(nm->module, inputs, input_count);
        for (int j = 0; j < input_count; j++) free(inputs[j]);
    }

    printf("=== Final module count: %d ===\n", module_count);
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
        Module* m = modules[i].module;

		if (strcmp(m->name, "input") == 0) {
			m->process(m, input, frames); // Feed raw audio input
		} else {
			float mixed_input[frames];
			memset(mixed_input, 0, sizeof(float) * frames);

			for (int j = 0; j < m->num_inputs; j++) {
				float* in_buf = m->inputs[j];
				if (!in_buf) continue;
				for (unsigned long k = 0; k < frames; k++) {
					mixed_input[k] += in_buf[k];
				}
			}

			for (unsigned long k = 0; k < frames; k++) {
				mixed_input[k] = tanhf(mixed_input[k]);
			}
			m->process(m, mixed_input, frames);
		}
    }

	for (int i = 0; i < module_count; i++) {
	    if (strcmp(modules[i].name, "out") == 0) {
		    memcpy(output, modules[i].module->output_buffer, sizeof(float) * frames);
			return;
		}
	}

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

const char* get_module_alias(int index) {
    if (index < 0 || index >= module_count) return NULL;
    return modules[index].name;
}


