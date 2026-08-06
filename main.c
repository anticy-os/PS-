#include "cpu.h"
#include "video.h"
#include "irq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--trace") == 0) {
            trace_enabled = true;
        } else {
            filename = argv[i];
        }
    }

    if (!filename) {
        fprintf(stderr, "Usage: %s [-v|--trace] <bios_file>\n", argv[0]);
        return 1;
    }

    CPU *cpu = calloc(1, sizeof(CPU));
    if (!cpu) {
        fprintf(stderr, "Failed to allocate CPU\n");
        return 1;
    }

    cpu->pc = 0xBFC00000;
    cpu->next_pc = 0xBFC00004;
    cpu->cop0[12] = 0x00400000;
    cpu->cop0[15] = 0x00000002;
    gpu_init(&cpu->gpu);
    dma_init(&cpu->dma);

    if (load_binary(filename, cpu, 0x1FC00000) == 0) {
        free(cpu);
        return 1;
    }

    Video video;
    if (!video_init(&video)) {
        free(cpu);
        return 1;
    }

    bool running = true;
    unsigned instructions_since_present = 0;
    while (running && cpu_step(cpu)) {
        if (++instructions_since_present == 2048) {
            running = video_update(&video, &cpu->gpu);
            irq_request(&cpu->irq, I_VBLANK);
            instructions_since_present = 0;
        }
    }

    if (trace_enabled) {
        printf("=== Final register state ===\n");
        dump_regs(cpu);
    }

    video_destroy(&video);
    free(cpu);
    return 0;
}
