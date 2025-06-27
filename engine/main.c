#include <stdio.h>
#include <portaudio.h>

#include "patch.h"
#include "module_loader.h"
#include "engine.h"

int main() {
    Patch patch = {
        .sample_rate = 44100.0f,
        .frame_count = 512,
        .module_count = 0
    };

    patch.modules[patch.module_count++] = load_module("./modules/vco/vco.dylib", patch.sample_rate);
    patch.modules[patch.module_count++] = load_module("./modules/moog_filter/moog_filter.dylib", patch.sample_rate);

    if (!patch.modules[0] || !patch.modules[1]) {
        fprintf(stderr, "Error loading modules.\n");
        return 1;
    }

    // Patch output of VCO to input of Moog Filter
    patch.modules[1]->connect_input(patch.modules[1], 0, patch.modules[0]->output);

    start_audio(&patch);

    printf("Running... press Enter to stop.\n");
    getchar();

    Pa_Terminate();
    return 0;
}

