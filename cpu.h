#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

#define RAM_SIZE 0x200000 // 2MB
#define GET_OPCODE(instruction) (((instruction) >> 26) & 0x3F)
#define GET_RS(instruction) (((instruction) >> 21) & 0x1F)
#define GET_RT(instruction) (((instruction) >> 16) & 0x1F)
#define GET_RD(instruction) (((instruction) >> 11) & 0x1F)
#define GET_FUNCT(instruction) ((instruction) & 0x3F)

#define EXC_INT      0
#define EXC_ADEL     4
#define EXC_ADES     5
#define EXC_SYSCALL  8
#define EXC_BP       9
#define EXC_RI       10
#define EXC_OV       12

typedef struct {
    uint32_t pc;
    uint32_t next_pc;
    uint32_t regs[32];
    uint32_t hi;
    uint32_t lo;
    uint32_t cop0[32];
    uint8_t ram[RAM_SIZE];

    bool in_delay_slot;
    bool next_in_delay_slot;
} CPU;

extern bool trace_enabled;

uint32_t load_binary(const char *filename, CPU *cpu, uint32_t load_addr);
uint32_t cpu_read32(CPU *cpu, uint32_t addr);

void cpu_write32(CPU *cpu, uint32_t addr, uint32_t val);
void dump_regs(CPU *cpu);

bool cpu_step(CPU *cpu);

#endif