#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool trace_enabled = false;

// MEMORY

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

// HELPING SHIT

static void set_reg(CPU *cpu, uint32_t reg, uint32_t val) {
    if (reg != 0) {
        cpu->regs[reg] = val;
    }
}

void dump_regs(CPU *cpu) {
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

// HLT (I'm not sure yet if this is how it supposed to work)

static bool execute_hlt(CPU *cpu) {
    printf("HALT at PC 0x%08X\n", cpu->pc);
    return true;
}

// ARITHMETIC

static void execute_add(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    int32_t res;
    if (add_overflow(rs, rt, &res)) {
        throw_exception(cpu, "Integer Overflow (ADD)");
    }
    set_reg(cpu, GET_RD(instruction), (uint32_t)res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d + $%d  -> $%d = %d (0x%08X)\n", __func__ + 8,
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_addu(CPU *cpu, uint32_t instruction) {
    uint32_t res = cpu->regs[GET_RS(instruction)] + cpu->regs[GET_RT(instruction)];
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d + $%d  -> $%d = %u (0x%08X)\n", __func__ + 8,
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
        printf("  [%s]  $%d = $%d + %d  -> $%d = %d (0x%08X)\n", __func__ + 8,
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
        printf("  [%s]  $%d = $%d + %d  -> $%d = %u (0x%08X)\n", __func__ + 8,
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
        printf("  [%s]  $%d = $%d - $%d  -> $%d = %d (0x%08X)\n", __func__ + 8,
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_subu(CPU *cpu, uint32_t instruction) {
    uint32_t res = cpu->regs[GET_RS(instruction)] - cpu->regs[GET_RT(instruction)];
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d - $%d  -> $%d = %u (0x%08X)\n", __func__ + 8,
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, res);
    }
}

// LOGIC 

static void execute_and(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = rs & rt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d & $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_andi(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = rs & imm;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d & 0x%04X  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, (uint32_t)res);
    }
}

static void execute_or(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = rs | rt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d | $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_ori(CPU *cpu, uint32_t instruction) {
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = cpu->regs[GET_RS(instruction)] | imm;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d | 0x%X  -> $%d = 0x%08X\n", __func__ + 8,
               GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res);
    }
}

static void execute_nor(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = ~(rs | rt);
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = ~($%d | $%d)  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_xor(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = rs ^ rt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d ^ $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_xori(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = rs ^ imm;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d ^ 0x%04X  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, (uint32_t)res);
    }
}

static void execute_lui(CPU *cpu, uint32_t instruction) {
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = imm << 16;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = 0x%04X << 16  -> $%d = 0x%08X\n", __func__ + 8,
                GET_RT(instruction), imm,
                GET_RT(instruction), res);
    }
}

// COMARISON 

static void execute_slt(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    uint32_t res = (rs < rt) ? 1 : 0;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d < $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_sltu(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = (rs < rt) ? 1 : 0;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d < $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
}

static void execute_slti(CPU *cpu, uint32_t instruction) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t res = (rs < (int32_t)imm) ? 1 : 0;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d < 0x%04X  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, (uint32_t)res);
    }
}

static void execute_sltiu(CPU *cpu, uint32_t instruction) {
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm_signed = (int16_t)(instruction & 0xFFFF);
    uint32_t imm = (uint32_t)(int32_t)imm_signed;
    uint32_t res = (rs < imm) ? 1 : 0;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d < %u (unsigned)  -> $%d = %u\n",
               __func__ + 8, GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res);
    }
}

// SHIFT

static void execute_sll(CPU *cpu, uint32_t instruction) {
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t shamt = (instruction >> 6) & 0x1F;
    uint32_t res = rt << shamt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d << %u  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), shamt,
               GET_RD(instruction), res);
    }
}

static void execute_srl(CPU *cpu, uint32_t instruction) {
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t shamt = (instruction >> 6) & 0x1F;
    uint32_t res = rt >> shamt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d >> %u  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), shamt,
               GET_RD(instruction), res);
    }
}

static void execute_sra(CPU *cpu, uint32_t instruction) {
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t shamt = (instruction >> 6) & 0x1F;
    uint32_t res = (uint32_t)((int32_t)rt >> shamt);
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d >> %u (arithmetic)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), shamt,
               GET_RD(instruction), res);
    }
}

// MEMORY ACCESS

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
        printf("  [%s]  $%d = mem[0x%08X]  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
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
        printf("  [%s]  mem[0x%08X] = $%d  -> 0x%08X\n",
               __func__ + 8, addr, GET_RT(instruction), val);
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
        printf("  [%s]  $%d = mem[0x%08X] (signed)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
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
        printf("  [%s]  $%d = mem[0x%08X] (unsigned)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
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
        printf("  [%s]  $%d = mem[0x%08X] (signed)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
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
        printf("  [%s]  $%d = mem[0x%08X] (unsigned)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
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
        printf("  [%s]  mem[0x%08X] = $%d  -> 0x%02X\n",
               __func__ + 8, addr, GET_RT(instruction), val);
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
        printf("  [%s]  mem[0x%08X] = $%d  -> 0x%04X\n",
               __func__ + 8, addr, GET_RT(instruction), val);
    }
}

// BRANCHES

static void execute_beq(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);

    if (rs == rt) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d == $%d, branch taken, next_pc=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), GET_RT(instruction), target);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d != $%d, branch not taken\n",
                   __func__ + 8, GET_RS(instruction), GET_RT(instruction));
        }
    }
}

static void execute_bne(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);

    if (rs != rt) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d != $%d, branch taken, next_pc=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), GET_RT(instruction), target);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d == $%d, branch not taken\n",
                   __func__ + 8, GET_RS(instruction), GET_RT(instruction));
        }
    }
}

// MAIN

bool cpu_step(CPU *cpu) {
    uint32_t current_pc = cpu->pc;
    uint32_t instruction = cpu_read32(cpu, current_pc);
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4;

    if (instruction == 0x00000000) {
        return cpu->pc < RAM_SIZE;
    }

    printf("PC: 0x%08X | Instr: 0x%08X\n", current_pc, instruction);
    switch (GET_OPCODE(instruction)) {
        case 0x00:
            switch (GET_FUNCT(instruction)) {
                case 0x00: execute_sll(cpu, instruction); break;
                case 0x02: execute_srl(cpu, instruction); break;
                case 0x03: execute_sra(cpu, instruction); break;
                case 0x20: execute_add(cpu, instruction); break;
                case 0x21: execute_addu(cpu, instruction); break;
                case 0x22: execute_sub(cpu, instruction); break;
                case 0x23: execute_subu(cpu, instruction); break;
                case 0x24: execute_and(cpu, instruction); break;
                case 0x25: execute_or(cpu, instruction); break;
                case 0x26: execute_xor(cpu, instruction); break;
                case 0x27: execute_nor(cpu, instruction); break;
                case 0x2A: execute_slt(cpu, instruction); break;
                case 0x2B: execute_sltu(cpu, instruction); break;
                default:
                    printf("Unknown funct: 0x%02X at PC 0x%08X\n", GET_FUNCT(instruction), cpu->pc);
                    return false;
            }
            break;
        case 0x04: execute_beq(cpu, instruction, current_pc); break;
        case 0x05: execute_bne(cpu, instruction, current_pc); break;
        case 0x08: execute_addi(cpu, instruction); break;
        case 0x09: execute_addiu(cpu, instruction); break;
        case 0x0A: execute_slti(cpu, instruction); break;
        case 0x0B: execute_sltiu(cpu, instruction); break;
        case 0x0C: execute_andi(cpu, instruction); break;
        case 0x0D: execute_ori(cpu, instruction); break;
        case 0x0E: execute_xori(cpu, instruction); break;
        case 0x0F: execute_lui(cpu, instruction); break;
        case 0x20: execute_lb(cpu, instruction); break;
        case 0x21: execute_lh(cpu, instruction); break;
        case 0x23: execute_lw(cpu, instruction); break;
        case 0x24: execute_lbu(cpu, instruction); break;
        case 0x25: execute_lhu(cpu, instruction); break;
        case 0x28: execute_sb(cpu, instruction); break;
        case 0x29: execute_sh(cpu, instruction); break;
        case 0x2B: execute_sw(cpu, instruction); break;
        case 0x3F:
            if (execute_hlt(cpu)) return false;
            break;
        default:
            printf("Unknown opcode: 0x%02X at PC 0x%08X\n", GET_OPCODE(instruction), cpu->pc);
            return false;
    }

    if (trace_enabled) {
        dump_regs(cpu);
        printf("\n");
    }

    return cpu->pc < RAM_SIZE;
}