#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define RAM_SIZE 0x200000 // 2MB
#define GET_OPCODE(instruction) (((instruction) >> 26) & 0x3F)
#define GET_RS(instruction) (((instruction) >> 21) & 0x1F)
#define GET_RT(instruction) (((instruction) >> 16) & 0x1F)
#define GET_RD(instruction) (((instruction) >> 11) & 0x1F)
#define GET_FUNCT(instruction) ((instruction) & 0x3F)

typedef struct {
    uint32_t pc;
    uint32_t regs[32];
    uint8_t ram[RAM_SIZE];
} CPU;

uint32_t load_binary(const char *filename, CPU *cpu, uint32_t load_addr);
uint32_t cpu_read32(CPU *cpu, uint32_t addr);

#endif
