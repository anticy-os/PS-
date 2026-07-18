#include "cpu.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

static void set_reg(CPU *cpu, uint32_t reg, uint32_t val) {
    if (reg != 0) {
        cpu->regs[reg] = val;
    }
}

static void throw_exception(const char *msg, uint32_t pc) {
    fprintf(stderr, "Exception at PC 0x%08X: %s\n", pc, msg);
    exit(1);
}

static bool add_overflow(int32_t a, int32_t b, int32_t *result) {
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
    uint32_t ur = ua + ub;
    *result = (int32_t)ur;
    return ((ua ^ ur) & (ub ^ ur)) >> 31;
}

static bool sub_overflow(int32_t a, int32_t b, int32_t *result) {
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
    uint32_t ur = ua - ub;
    *result = (int32_t)ur;
    return ((ua ^ ub) & (ua ^ ur)) >> 31;
}

static void execute_add(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    int32_t res;
    if (add_overflow(rs, rt, &res)) {
        throw_exception("Integer Overflow (ADD)", cpu->pc);
    }
    set_reg(cpu, GET_RD(instruction), (uint32_t)res);
}

static void execute_addu(CPU *cpu, uint32_t instruction) {
    set_reg(cpu, GET_RD(instruction), cpu->regs[GET_RS(instruction)] + cpu->regs[GET_RT(instruction)]);
}

static void execute_addi(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    int32_t res;
    if (add_overflow(rs, imm, &res)) {
        throw_exception("Integer Overflow (ADDI)", cpu->pc);
    }
    set_reg(cpu, GET_RT(instruction), (uint32_t)res);
}

static void execute_addiu(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    set_reg(cpu, GET_RT(instruction), (uint32_t)(rs + imm));
}

static void execute_sub(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    int32_t res;
    if (sub_overflow(rs, rt, &res)) {
        throw_exception("Integer Overflow (SUB)", cpu->pc);
    }
    set_reg(cpu, GET_RD(instruction), (uint32_t)res);
}

static void execute_subu(CPU *cpu, uint32_t instruction) {
    set_reg(cpu, GET_RD(instruction), cpu->regs[GET_RS(instruction)] - cpu->regs[GET_RT(instruction)]);
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(stderr, "Usage: %s <binary_file>\n", argv[0]);
        return 1;
    }
    CPU *cpu = calloc(1, sizeof(CPU));
    if (!cpu) {
        fprintf(stderr, "Failed to allocate CPU\n");
        return 1;
    }

    if (load_binary(argv[1], cpu, 0) == 0) {
        free(cpu);
        return 1;
    }

     while (1) {
        uint32_t instruction = cpu_read32(cpu, cpu->pc);
        if (instruction == 0x00000000) {
            cpu->pc += 4;
            if (cpu->pc >= RAM_SIZE) break;
            continue;
        }

        printf("PC: 0x%08X | Instr: 0x%08X\n", cpu->pc, instruction);
        switch (GET_OPCODE(instruction)) {
            case 0x00:
                switch (GET_FUNCT(instruction)) {
                    case 0x20: execute_add(cpu, instruction); break;
                    case 0x21: execute_addu(cpu, instruction); break;
                    case 0x22: execute_sub(cpu, instruction); break;
                    case 0x23: execute_subu(cpu, instruction); break;
                    default:
                        printf("Unknown funct: 0x%02X at PC 0x%08X\n", GET_FUNCT(instruction), cpu->pc);
                        free(cpu);
                        return 1;
                }
                break;
            case 0x08: execute_addi(cpu, instruction); break;
            case 0x09: execute_addiu(cpu, instruction); break;
            default:
                printf("Unknown opcode: 0x%02X at PC 0x%08X\n", GET_OPCODE(instruction), cpu->pc);
                free(cpu);
                return 1;
        }

        cpu->pc += 4;
        if (cpu->pc >= RAM_SIZE) break;
    }

    free(cpu);
    return 0;
}