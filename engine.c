#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <portaudio.h>

#include "engine.h"
#include "module_loader.h"
#include "util.h"
#include "./modules/vca/vca.h"
#include "./modules/input/input.h"
#include "./modules/c_output/c_output.h"
#include "./modules/c_input/c_input.h"

int ui_enabled = 1;
static NamedModule modules[MAX_MODULES];
static int module_count = 0;
int g_num_output_channels = 2;
int g_num_input_channels = 1;
extern float sample_rate;


typedef struct {
    char modtype[64];
    char alias[64];
    char input_str[16384];
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
    m->num_inputs = 0;
    m->num_control_inputs = 0;

    for (int i = 0; i < input_count && i < MAX_INPUTS; i++) {
        char* name = input_names[i];
        char* equals = strchr(name, '=');

        if (equals) {
            *equals = '\0';
            char* source_name = equals + 1;

            NamedModule* src = find_module_by_name(source_name);
            if (src && strncmp(src->name, "c_", 2) == 0 && src->module->control_output) {
                if (m->num_control_inputs < MAX_CONTROL_INPUTS) {
                    m->control_inputs[m->num_control_inputs++] = src->module->control_output;
                } else {
                    fprintf(stderr, "Too many control inputs\n");
                }
            } else if (src && src->module->output_buffer) {
                if (m->num_inputs < MAX_INPUTS) {
                    m->inputs[m->num_inputs++] = src->module->output_buffer;
                } else {
                    fprintf(stderr, "Too many audio inputs\n");
                }
            } else {
                fprintf(stderr, "Unknown or invalid input module: %s\n", source_name);
            }
        } else {
            NamedModule* src = find_module_by_name(name);
            if (src && src->module->output_buffer) {
                if (m->num_inputs < MAX_INPUTS) {
                    m->inputs[m->num_inputs++] = src->module->output_buffer;
                } else {
                    fprintf(stderr, "Too many audio inputs\n");
                }
            } else {
                fprintf(stderr, "Error: unknown input module '%s'\n", name);
            }
        }
    }
}

static void connect_control_inputs(Module* m, char** param_names, char** source_names, int count) {
    for (int i = 0; i < count && i < MAX_CONTROL_INPUTS; i++) {
		NamedModule* src = find_module_by_name(source_names[i]);
		if (src && src->module && src->module->control_output) {
			m->control_inputs[m->num_control_inputs] = src->module->control_output;
			m->control_input_params[m->num_control_inputs] = strdup(param_names[i]);
			m->num_control_inputs++;
		} else {
			// Try interpreting as a literal float
			char* endptr = NULL;
			float literal = strtof(source_names[i], &endptr);
			if (endptr && *endptr == '\0') {
				float* buffer = malloc(sizeof(float) * FRAMES_PER_BUFFER);
				for (int j = 0; j < FRAMES_PER_BUFFER; j++) buffer[j] = literal;
				m->control_inputs[m->num_control_inputs] = buffer;
				m->control_input_params[m->num_control_inputs] = strdup(param_names[i]);
				m->num_control_inputs++;
			} else {
				fprintf(stderr, "Error: invalid control source '%s'\n", source_names[i]);
			}
		}
    }
}

static void parse_patch_line(const char* line) {
    char modtype[64] = {0};
    char alias[64] = {0};
    char all_args[16384] = {0};      // Full contents inside ( )
    char create_args[16384] = {0};   // [file=snd.wav]
    char input_str[16384] = {0};     // speed=l1

    if (strstr(line, "(")) {
        sscanf(line, "%[^ (](%[^)]) as %s", modtype, all_args, alias);

        // Now separate [ ... ] from the rest
        const char* bracket_start = strchr(all_args, '[');
        const char* bracket_end   = strchr(all_args, ']');
        if (bracket_start && bracket_end && bracket_end > bracket_start) {
            size_t len = bracket_end - bracket_start - 1;
            strncpy(create_args, bracket_start + 1, len);
            create_args[len] = '\0';

            // Copy rest (after ]) into input_str
            const char* rest = bracket_end + 1;
            while (*rest == ',' || *rest == ' ') rest++;  // skip comma/space
            strncpy(input_str, rest, sizeof(input_str) - 1);
            input_str[sizeof(input_str) - 1] = '\0';
        } else {
            // no brackets found, treat all as input_str
            strncpy(input_str, all_args, sizeof(input_str));
        }
    } else if (strstr(line, "as")) {
        sscanf(line, "%s as %s", modtype, alias);
    } else {
        sscanf(line, "%s", modtype);
        snprintf(alias, sizeof(alias), "%s%d", modtype, module_count);
    }

	// Detect "outN" alias and add channel number as argument (for VCA modules)
	if (strncmp(alias, "out", 3) == 0 && isdigit(alias[3])) {
		int ch = atoi(alias + 3);
		char append[32];
		snprintf(append, sizeof(append), " ch=%d", ch);
		strncat(create_args, append, sizeof(create_args) - strlen(create_args) - 1);
	}



    // Call load_module with create_args
    Module* m = load_module(modtype, sample_rate, create_args);
    if (!m) {
        fprintf(stderr, "Failed to load module: %s\n", modtype);
        return;
    }

    m->name = strdup(alias);

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
	ui_enabled = 1; // Default ON
	if (strstr(patch_text, "no_ui")) {
        fprintf(stderr, "[engine] UI disabled (no_ui flag found)\n");
        ui_enabled = 0;
    }

    char* patch = strdup(patch_text);
    char* line = strtok(patch, "\r\n");

	while (line) {
		char* clean_line = trim_whitespace(line);

		// --- Skip blank lines, comments, and directives like "no_ui"
		if (strlen(clean_line) == 0 ||
			clean_line[0] == '#' ||
			strncmp(clean_line, "//", 2) == 0 ||
			strncasecmp(clean_line, "no_ui", 5) == 0) {
			line = strtok(NULL, "\r\n");
			continue;
		}

		parse_patch_line(clean_line);
		line = strtok(NULL, "\r\n");
	}

    // Second pass: connect inputs
    for (int i = 0; i < patch_line_count; i++) {
        NamedModule* nm = find_module_by_name(patch_lines[i].alias);
        if (!nm) continue;

        char* token;
        char input_buf[16384];
        strncpy(input_buf, patch_lines[i].input_str, sizeof(input_buf));

        char* audio_inputs[MAX_INPUTS];
        int audio_input_count = 0;
		
		char* control_param_names[MAX_CONTROL_INPUTS];
		char* control_source_names[MAX_CONTROL_INPUTS];
		int control_input_count = 0;

        token = strtok(input_buf, ",");
        while (token) {
			char* trimmed = trim_whitespace(token);
			char* eq = strchr(trimmed, '=');

			if (eq) {
				*eq = '\0';
				control_param_names[control_input_count] = strdup(trim_whitespace(trimmed));
				control_source_names[control_input_count] = strdup(trim_whitespace(eq+1));
				control_input_count++;
			} else {
				audio_inputs[audio_input_count++] = strdup(trimmed);
			}
			token = strtok(NULL, ",");
		}
		connect_module_inputs(nm->module, audio_inputs, audio_input_count);
		connect_control_inputs(nm->module, control_param_names, control_source_names, control_input_count);

        for (int j=0; j<audio_input_count; j++) free(audio_inputs[j]);
        for (int j=0; j<control_input_count; j++) {
			free(control_param_names[j]);
			free(control_source_names[j]);
		}

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
	// Control logic
	for (int i=0; i<module_count; i++) {
		Module* m = modules[i].module;
		if (m->process_control) {
			m->process_control(m);
		}
	}
    for (int i=0; i<module_count; i++) {
        Module* m = modules[i].module;

		if (m->type && (strcmp(m->type, "input") == 0 ||
			 strcmp(m->type, "c_input") == 0)) {
			int ch;
			unsigned long stride = g_num_input_channels;

			// Correct struct per module type
			if (strcmp(m->type, "input") == 0) {
				InputState* s = (InputState*)m->state;
				ch = s->channel_index;
			} else {
				CInputState* s = (CInputState*)m->state;
				ch = s->channel_index;
			}

			if (ch < 1 || ch > stride) ch = 1;

			float tmp[frames];
			for (unsigned long k = 0; k < frames; k++)
				tmp[k] = input[k * stride + (ch - 1)];

			m->process(m, tmp, frames);
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

			if (m->num_inputs > 0) {
				float norm = 1.0f / (float)m->num_inputs;
				for (unsigned long k = 0; k < frames; k++) {
					mixed_input[k] *= norm;
			 }
		}
			if (m->process) {
				m->process(m, mixed_input, frames);
			}
		}
    }

	double block_ms = (1000.0 * frames / sample_rate);
	scheduler_tick(block_ms);

	// --- Final multi-channel mixdown ---
	int num_channels = g_num_output_channels;
	const PaDeviceInfo* info = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice());
	if (info && info->maxOutputChannels > 2)
		num_channels = info->maxOutputChannels;

	memset(output, 0, sizeof(float) * frames * num_channels);

	for (int i = 0; i < module_count; i++) {
		Module* m = modules[i].module;

		if (strcmp(m->type, "vca") == 0) {
			// handle channel-mapped VCAs only
			VCAState* s = (VCAState*)m->state;
			int ch = (s && s->target_channel > 0 && s->target_channel <= num_channels)
						? s->target_channel
						: -1;

			for (unsigned long k = 0; k < frames; k++) {
				if (ch > 0 && ch <= num_channels)
					output[k * num_channels + (ch - 1)] += m->output_bufferL[k];
				else {
					output[k * num_channels] += m->output_bufferL[k];
					if (num_channels > 1)
						output[k * num_channels + 1] += m->output_bufferR[k];
				}
			}
		}
		else if (strcmp(m->type, "c_output") == 0) {
			// handle channel-mapped c_output modules
			COutputState* s = (COutputState*)m->state;
			int ch = (s && s->target_channel > 0 && s->target_channel <= num_channels)
						? s->target_channel
						: -1;

			for (unsigned long k = 0; k < frames; k++) {
				if (ch > 0 && ch <= num_channels)
					output[k * num_channels + (ch - 1)] += m->output_bufferL[k];
				else {
					output[k * num_channels] += m->output_bufferL[k];
					if (num_channels > 1)
						output[k * num_channels + 1] += m->output_bufferR[k];
				}
			}
		}
		else if (strcmp(modules[i].name, "out") == 0) {
			// normal stereo master out
			float* outL = m->output_bufferL ? m->output_bufferL : m->output_buffer;
			float* outR = m->output_bufferR ? m->output_bufferR : m->output_buffer;

			for (unsigned long k = 0; k < frames; k++) {
				output[k * num_channels] = outL[k];
				if (num_channels > 1)
					output[k * num_channels + 1] = outR[k];
			}
		}

	}
	// --- Normalize global output to prevent clipping ---
	float max_val = 0.0f;
	for (unsigned long i = 0; i < frames * num_channels; i++) {
		if (fabsf(output[i]) > max_val) max_val = fabsf(output[i]);
	}
	if (max_val > 1.0f && max_val < 100.0f) {
		float norm = 1.0f / max_val;
		for (unsigned long i = 0; i < frames * num_channels; i++)
			output[i] *= norm;
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


