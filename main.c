#include "cpu.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool trace_enabled = false;

static void set_reg(CPU *cpu, uint32_t reg, uint32_t val) {
    if (reg != 0) {
        cpu->regs[reg] = val;
    }
}

static void dump_regs(CPU *cpu) {
    for (int i = 0; i < 32; i++) {
        printf("$%-2d=0x%08X ", i, cpu->regs[i]);
        if (i % 4 == 3) printf("\n");
    }
}

static void throw_exception(CPU *cpu, const char *msg) {
    fprintf(stderr, "Exception at PC 0x%08X: %s\n", cpu->pc, msg);
    if (trace_enabled) {
        fprintf(stderr, "--- Regs at exception ---\n");
        dump_regs(cpu);
    }
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

static bool execute_hlt(CPU *cpu){
    printf("HALT at PC 0x%08X\n", cpu->pc);
    return true; 
}

static void execute_add(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    int32_t res;
    if (add_overflow(rs, rt, &res)) {
        throw_exception(cpu, "Integer Overflow (ADD)");
    }
    set_reg(cpu, GET_RD(instruction), (uint32_t)res);
    if (trace_enabled) {
        printf("  ADD  $%d = $%d + $%d  -> $%d = %d (0x%08X)\n",
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_addu(CPU *cpu, uint32_t instruction) {
    uint32_t res = cpu->regs[GET_RS(instruction)] + cpu->regs[GET_RT(instruction)];
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  ADDU $%d = $%d + $%d  -> $%d = %u (0x%08X)\n",
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, res);
    }
}

static void execute_addi(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    int32_t res;
    if (add_overflow(rs, imm, &res)) {
        throw_exception(cpu, "Integer Overflow (ADDI)");
    }
    set_reg(cpu, GET_RT(instruction), (uint32_t)res);
    if (trace_enabled) {
        printf("  ADDI $%d = $%d + %d  -> $%d = %d (0x%08X)\n",
               GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, (uint32_t)res);
    }
}

static void execute_addiu(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t res = (uint32_t)(rs + imm);
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  ADDIU $%d = $%d + %d  -> $%d = %u (0x%08X)\n",
               GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, res);
    }
}

static void execute_sub(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    int32_t res;
    if (sub_overflow(rs, rt, &res)) {
        throw_exception(cpu, "Integer Overflow (SUB)");
    }
    set_reg(cpu, GET_RD(instruction), (uint32_t)res);
    if (trace_enabled) {
        printf("  SUB  $%d = $%d - $%d  -> $%d = %d (0x%08X)\n",
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_subu(CPU *cpu, uint32_t instruction) {
    uint32_t res = cpu->regs[GET_RS(instruction)] - cpu->regs[GET_RT(instruction)];
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  SUBU $%d = $%d - $%d  -> $%d = %u (0x%08X)\n",
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, res);
    }
}

static void execute_ori(CPU *cpu, uint32_t instruction) {
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = cpu->regs[GET_RS(instruction)] | imm;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  ORI  $%d = $%d | 0x%X  -> $%d = 0x%08X\n",
               GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res);
    }
}

static void execute_lui(CPU *cpu, uint32_t instruction) {
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = imm << 16;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  LUI  $%d = 0x%04X << 16  -> $%d = 0x%08X\n",
                GET_RT(instruction), imm,
                GET_RT(instruction), res);
    }
}

static void execute_lw(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 4 != 0) {
        throw_exception(cpu, "Unaligned memory access (LW)");
        return;
    }
    if (addr > RAM_SIZE - 4) {
        throw_exception(cpu, "Address out of bounds (LW)");
        return;
    }
    uint32_t val = cpu_read32(cpu, addr);
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  LW   $%d = mem[0x%08X]  -> $%d = 0x%08X\n",
               GET_RT(instruction), addr, GET_RT(instruction), val);
    }
}

static void execute_sw(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 4 != 0) {
        throw_exception(cpu, "Unaligned memory access (SW)");
        return;
    }
    if (addr > RAM_SIZE - 4) {
        throw_exception(cpu, "Address out of bounds (SW)");
        return;
    }
    uint32_t val = cpu->regs[GET_RT(instruction)];
    cpu_write32(cpu, addr, val);
    if (trace_enabled) {
        printf("  SW   mem[0x%08X] = $%d  -> 0x%08X\n",
               addr, GET_RT(instruction), val);
    }
}

static void execute_lb(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr >= RAM_SIZE) {
        throw_exception(cpu, "Address out of bounds (LB)");
        return;
    }
    int8_t byte = (int8_t)cpu->ram[addr];
    uint32_t val = (uint32_t)(int32_t)byte;
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  LB   $%d = mem[0x%08X] (signed)  -> $%d = 0x%08X\n",
               GET_RT(instruction), addr, GET_RT(instruction), val);
    }
}

static void execute_lbu(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr >= RAM_SIZE) {
        throw_exception(cpu, "Address out of bounds (LBU)");
        return;
    }
    uint32_t val = (uint32_t)cpu->ram[addr];
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  LBU  $%d = mem[0x%08X] (unsigned)  -> $%d = 0x%08X\n",
               GET_RT(instruction), addr, GET_RT(instruction), val);
    }
}

static void execute_lh(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 2 != 0) {
        throw_exception(cpu, "Unaligned memory access (LH)");
        return;
    }
    if (addr > RAM_SIZE - 2) {
        throw_exception(cpu, "Address out of bounds (LH)");
        return;
    }
    uint16_t half = (uint16_t)(cpu->ram[addr] | (cpu->ram[addr + 1] << 8));
    uint32_t val = (uint32_t)(int32_t)(int16_t)half; 
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  LH   $%d = mem[0x%08X] (signed)  -> $%d = 0x%08X\n",
               GET_RT(instruction), addr, GET_RT(instruction), val);
    }
}

static void execute_lhu(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 2 != 0) {
        throw_exception(cpu, "Unaligned memory access (LHU)");
        return;
    }
    if (addr > RAM_SIZE - 2) {
        throw_exception(cpu, "Address out of bounds (LHU)");
        return;
    }
    uint32_t val = (uint32_t)(cpu->ram[addr] | (cpu->ram[addr + 1] << 8));
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  LHU  $%d = mem[0x%08X] (unsigned)  -> $%d = 0x%08X\n",
               GET_RT(instruction), addr, GET_RT(instruction), val);
    }
}

static void execute_sb(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr >= RAM_SIZE) {
        throw_exception(cpu, "Address out of bounds (SB)");
        return;
    }
    uint8_t val = (uint8_t)(cpu->regs[GET_RT(instruction)] & 0xFF);
    cpu->ram[addr] = val;
    if (trace_enabled) {
        printf("  SB   mem[0x%08X] = $%d  -> 0x%02X\n",
               addr, GET_RT(instruction), val);
    }
}

static void execute_sh(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 2 != 0) {
        throw_exception(cpu, "Unaligned memory access (SH)");
        return;
    }
    if (addr > RAM_SIZE - 2) {
        throw_exception(cpu, "Address out of bounds (SH)");
        return;
    }
    uint16_t val = (uint16_t)(cpu->regs[GET_RT(instruction)] & 0xFFFF);
    cpu->ram[addr]     = val & 0xFF;
    cpu->ram[addr + 1] = (val >> 8) & 0xFF;
    if (trace_enabled) {
        printf("  SH   mem[0x%08X] = $%d  -> 0x%04X\n",
               addr, GET_RT(instruction), val);
    }
}

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

    if (load_binary(filename, cpu, 0) == 0) {
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
            case 0x0F: execute_lui(cpu, instruction); break;
            case 0x08: execute_addi(cpu, instruction); break;
            case 0x09: execute_addiu(cpu, instruction); break;
            case 0x0D: execute_ori(cpu, instruction); break;
            case 0x20: execute_lb(cpu, instruction); break;
            case 0x21: execute_lh(cpu, instruction); break;
            case 0x23: execute_lw(cpu, instruction); break;
            case 0x24: execute_lbu(cpu, instruction); break;
            case 0x25: execute_lhu(cpu, instruction); break;
            case 0x28: execute_sb(cpu, instruction); break;
            case 0x29: execute_sh(cpu, instruction); break;
            case 0x2B: execute_sw(cpu, instruction); break;
            case 0x3F: if (execute_hlt(cpu)) { free(cpu); return 0; } break;
            default:
                printf("Unknown opcode: 0x%02X at PC 0x%08X\n", GET_OPCODE(instruction), cpu->pc);
                free(cpu);
                return 1;
        }

        if (trace_enabled) {
            dump_regs(cpu);
            printf("\n");
        }

        cpu->pc += 4;
        if (cpu->pc >= RAM_SIZE) break;
    }

    if (trace_enabled) {
        printf("=== Final register state ===\n");
        dump_regs(cpu);
    }

    free(cpu);
    return 0;
}