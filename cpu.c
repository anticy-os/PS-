#include "cpu.h"
#include <stdio.h>
#include <string.h>

uint32_t cpu_read32(CPU *cpu, uint32_t addr) {
    if (addr > RAM_SIZE - 4) return 0;
    uint8_t *ptr = &cpu->ram[addr];
    return (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
}

void cpu_write32(CPU *cpu, uint32_t addr, uint32_t val) {
    if (addr > RAM_SIZE - 4) return; 
    uint8_t *ptr = &cpu->ram[addr];
    ptr[0] = val & 0xFF;
    ptr[1] = (val >> 8) & 0xFF;
    ptr[2] = (val >> 16) & 0xFF;
    ptr[3] = (val >> 24) & 0xFF;
}

uint32_t load_binary(const char *filename, CPU *cpu, uint32_t load_addr) {
    if (load_addr >= RAM_SIZE) {
        fprintf(stderr, "load_addr out of range\n");
        return 0;
    }
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open binary file");
        return 0;
    }
    size_t max_len = RAM_SIZE - load_addr;
    size_t bytesRead = fread(&cpu->ram[load_addr], 1, max_len, file);
    printf("Loaded %zu bytes into memory\n", bytesRead);
    fclose(file);
    return (uint32_t)bytesRead;
}