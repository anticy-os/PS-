#include "cpu.h"
#include <stdio.h>
#include <string.h>

uint32_t cpu_read32(CPU *cpu, uint32_t addr) {
    if (addr > RAM_SIZE - 4) return 0;
    
    uint8_t *ptr = &cpu->ram[addr];
    return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
}

void cpu_write32(CPU *cpu, uint32_t addr, uint32_t val) {
    if (addr > RAM_SIZE - 4) {
        // Gotta add excption handling later
    return;
    }
    uint8_t *ptr = &cpu->ram[addr & 0x1FFFFF];
    ptr[0] = val & 0xFF;
    ptr[1] = (val >> 8) & 0xFF;
    ptr[2] = (val >> 16) & 0xFF;
    ptr[3] = (val >> 24) & 0xFF;
}

uint32_t load_binary(const char *filename, CPU *cpu, uint32_t load_addr) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open binary file");
        return 0;
    }
    
    size_t bytesRead = fread(&cpu->ram[load_addr], 1, RAM_SIZE - load_addr, file);
    printf("Loaded %zu bytes into memory\n", bytesRead);
    fclose(file);
    return (uint32_t)bytesRead;
}