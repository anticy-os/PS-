#include "cpu.h"
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
        fprintf(stderr, "Usage: %s [-v|--trace] <binary_file>\n", argv[0]);
        return 1;
    }

    CPU *cpu = calloc(1, sizeof(CPU));
    if (!cpu) {
        fprintf(stderr, "Failed to allocate CPU\n");
        return 1;
    }

    cpu->pc = 0;
    cpu->next_pc = 4;

    if (load_binary(filename, cpu, 0) == 0) {
        free(cpu);
        return 1;
    }

    while (cpu_step(cpu)) {
    }

    if (trace_enabled) {
        printf("=== Final register state ===\n");
        dump_regs(cpu);
    }

    free(cpu);
    return 0;
}