#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

typedef bool (*InstrFn)(CPU *cpu, uint32_t instruction, uint32_t current_pc);

// HLT (I'm not sure yet if this is how it supposed to work)

static bool execute_hlt(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)instruction; (void)current_pc;
    printf("HALT at PC 0x%08X\n", cpu->pc);
    return false;
}

// ARITHMETIC

static bool execute_add(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
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
    return true;
}

static bool execute_addu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t res = cpu->regs[GET_RS(instruction)] + cpu->regs[GET_RT(instruction)];
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d + $%d  -> $%d = %u (0x%08X)\n", __func__ + 8,
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, res);
    }
    return true;
}

static bool execute_addi(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
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
    return true;
}

static bool execute_addiu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t res = (uint32_t)(rs + imm);
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d + %d  -> $%d = %u (0x%08X)\n", __func__ + 8,
               GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, res);
    }
    return true;
}

static bool execute_sub(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
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
    return true;
}

static bool execute_subu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t res = cpu->regs[GET_RS(instruction)] - cpu->regs[GET_RT(instruction)];
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d - $%d  -> $%d = %u (0x%08X)\n", __func__ + 8,
               GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, res);
    }
    return true;
}

static bool execute_mult(CPU *cpu, uint32_t instruction, uint32_t current_pc){
    (void) current_pc;
    int32_t rs = cpu->regs[GET_RS(instruction)];
    int32_t rt = cpu->regs[GET_RT(instruction)];
    int64_t res = (int64_t)rs * (int64_t)rt;
    uint64_t ures = (uint64_t)res; 
    cpu->hi = (uint32_t)(ures >> 32);
    cpu->lo = (uint32_t)(ures & 0xFFFFFFFF);
    if (trace_enabled) {
        printf("  [%s]  HI:LO = $%d * $%d  -> HI=0x%08X LO=0x%08X\n",
               __func__ + 8, GET_RS(instruction), GET_RT(instruction), cpu->hi, cpu->lo);
    }
    return true;
}

static bool execute_multu(CPU *cpu, uint32_t instruction, uint32_t current_pc){
    (void) current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint64_t res = (uint64_t)rs * (uint64_t)rt;
    cpu->hi = (uint32_t)(res >> 32);
    cpu->lo = (uint32_t)(res & 0xFFFFFFFF);
    if (trace_enabled) {
        printf("  [%s]  HI:LO = $%d * $%d  -> HI=0x%08X LO=0x%08X\n",
               __func__ + 8, GET_RS(instruction), GET_RT(instruction), cpu->hi, cpu->lo);
    }
    return true;
}

static bool execute_div(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    int32_t rs = cpu->regs[GET_RS(instruction)];
    int32_t rt = cpu->regs[GET_RT(instruction)];
    if (rt == 0) {
        if (trace_enabled) printf("  [%s]  division by zero\n", __func__ + 8);
        return true;
    }
    if (rs == INT32_MIN && rt == -1) {
        cpu->lo = (uint32_t)INT32_MIN;
        cpu->hi = 0;
        return true;
    }
    cpu->lo = (uint32_t)(rs / rt);
    cpu->hi = (uint32_t)(rs % rt);
    if (trace_enabled) {
        printf("  [%s]  LO = $%d / $%d, HI = $%d %% $%d  -> LO=0x%08X HI=0x%08X\n",
               __func__ + 8, GET_RS(instruction), GET_RT(instruction),
               GET_RS(instruction), GET_RT(instruction), cpu->lo, cpu->hi);
    }
    return true;
}

static bool execute_divu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    if (rt == 0) {
        if (trace_enabled) printf("  [%s]  division by zero\n", __func__ + 8);
        return true;
    }
    cpu->lo = rs / rt;
    cpu->hi = rs % rt;
    if (trace_enabled) {
        printf("  [%s]  LO = $%d / $%d, HI = $%d %% $%d (unsigned)  -> LO=0x%08X HI=0x%08X\n",
               __func__ + 8, GET_RS(instruction), GET_RT(instruction),
               GET_RS(instruction), GET_RT(instruction), cpu->lo, cpu->hi);
    }
    return true;
}

static bool execute_mfhi(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    set_reg(cpu, GET_RD(instruction), cpu->hi);
    if (trace_enabled) {
        printf("  [%s]  $%d = HI  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RD(instruction), cpu->hi);
    }
    return true;
}

static bool execute_mflo(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    set_reg(cpu, GET_RD(instruction), cpu->lo);
    if (trace_enabled) {
        printf("  [%s]  $%d = LO  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RD(instruction), cpu->lo);
    }
    return true;
}

static bool execute_mthi(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    cpu->hi = rs;
    if (trace_enabled) {
        printf("  [%s]  HI = $%d  -> HI = 0x%08X\n",
               __func__ + 8, GET_RS(instruction), cpu->hi);
    }
    return true;
}


static bool execute_mtlo(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    cpu->lo = rs;
    if (trace_enabled) {
        printf("  [%s]  LO = $%d  -> LO = 0x%08X\n",
               __func__ + 8, GET_RS(instruction), cpu->lo);
    }
    return true;
}

// LOGIC

static bool execute_and(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = rs & rt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d & $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_andi(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = rs & imm;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d & 0x%04X  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_or(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = rs | rt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d | $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_ori(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = cpu->regs[GET_RS(instruction)] | imm;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d | 0x%X  -> $%d = 0x%08X\n", __func__ + 8,
               GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res);
    }
    return true;
}

static bool execute_nor(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = ~(rs | rt);
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = ~($%d | $%d)  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_xor(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = rs ^ rt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d ^ $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_xori(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = rs ^ imm;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d ^ 0x%04X  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_lui(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t imm = instruction & 0xFFFF;
    uint32_t res = imm << 16;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = 0x%04X << 16  -> $%d = 0x%08X\n", __func__ + 8,
                GET_RT(instruction), imm,
                GET_RT(instruction), res);
    }
    return true;
}

// COMARISON

static bool execute_slt(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int32_t rt = (int32_t)cpu->regs[GET_RT(instruction)];
    uint32_t res = (rs < rt) ? 1 : 0;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d < $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_sltu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t res = (rs < rt) ? 1 : 0;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d < $%d  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RD(instruction), GET_RS(instruction), GET_RT(instruction),
               GET_RD(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_slti(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t res = (rs < (int32_t)imm) ? 1 : 0;
    set_reg(cpu, GET_RT(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d < 0x%04X  -> $%d = %u (0x%08X)\n",
               __func__ + 8, GET_RT(instruction), GET_RS(instruction), imm,
               GET_RT(instruction), res, (uint32_t)res);
    }
    return true;
}

static bool execute_sltiu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
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
    return true;
}

// SHIFT

static bool execute_sll(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t shamt = (instruction >> 6) & 0x1F;
    uint32_t res = rt << shamt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d << %u  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), shamt,
               GET_RD(instruction), res);
    }
    return true;
}

static bool execute_sllv(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t shamt = rs & 0x1F;
    uint32_t res = rt << shamt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d << $%d  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), GET_RS(instruction), GET_RD(instruction), res);
    }
    return true;
}

static bool execute_srl(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t shamt = (instruction >> 6) & 0x1F;
    uint32_t res = rt >> shamt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d >> %u  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), shamt,
               GET_RD(instruction), res);
    }
    return true;
}

static bool execute_srlv(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t shamt = rs & 0x1F;
    uint32_t res = rt >> shamt;
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d >> $%d  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), GET_RS(instruction), GET_RD(instruction), res);
    }
    return true;
}

static bool execute_sra(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t shamt = (instruction >> 6) & 0x1F;
    uint32_t res = (uint32_t)((int32_t)rt >> shamt);
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d >> %u (arithmetic)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), shamt,
               GET_RD(instruction), res);
    }
    return true;
}

static bool execute_srav(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    uint32_t shamt = rs & 0x1F;
    uint32_t res = (uint32_t)((int32_t)rt >> shamt);
    set_reg(cpu, GET_RD(instruction), res);
    if (trace_enabled) {
        printf("  [%s]  $%d = $%d >> $%d (arithmetic)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RD(instruction), GET_RT(instruction), GET_RS(instruction), GET_RD(instruction), res);
    }
    return true;
}

// MEMORY ACCESS

static bool execute_lw(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 4 != 0) {
        throw_exception(cpu, "Unaligned memory access (LW)");
        return true;
    }
    if (addr > RAM_SIZE - 4) {
        throw_exception(cpu, "Address out of bounds (LW)");
        return true;
    }
    uint32_t val = cpu_read32(cpu, addr);
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X]  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_sw(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 4 != 0) {
        throw_exception(cpu, "Unaligned memory access (SW)");
        return true;
    }
    if (addr > RAM_SIZE - 4) {
        throw_exception(cpu, "Address out of bounds (SW)");
        return true;
    }
    uint32_t val = cpu->regs[GET_RT(instruction)];
    cpu_write32(cpu, addr, val);
    if (trace_enabled) {
        printf("  [%s]  mem[0x%08X] = $%d  -> 0x%08X\n",
               __func__ + 8, addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_lb(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr >= RAM_SIZE) {
        throw_exception(cpu, "Address out of bounds (LB)");
        return true;
    }
    int8_t byte = (int8_t)cpu->ram[addr];
    uint32_t val = (uint32_t)(int32_t)byte;
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X] (signed)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_lbu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr >= RAM_SIZE) {
        throw_exception(cpu, "Address out of bounds (LBU)");
        return true;
    }
    uint32_t val = (uint32_t)cpu->ram[addr];
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X] (unsigned)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_lh(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 2 != 0) {
        throw_exception(cpu, "Unaligned memory access (LH)");
        return true;
    }
    if (addr > RAM_SIZE - 2) {
        throw_exception(cpu, "Address out of bounds (LH)");
        return true;
    }
    uint16_t half = (uint16_t)(cpu->ram[addr] | (cpu->ram[addr + 1] << 8));
    uint32_t val = (uint32_t)(int32_t)(int16_t)half;
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X] (signed)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_lhu(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 2 != 0) {
        throw_exception(cpu, "Unaligned memory access (LHU)");
        return true;
    }
    if (addr > RAM_SIZE - 2) {
        throw_exception(cpu, "Address out of bounds (LHU)");
        return true;
    }
    uint32_t val = (uint32_t)(cpu->ram[addr] | (cpu->ram[addr + 1] << 8));
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X] (unsigned)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_sb(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr >= RAM_SIZE) {
        throw_exception(cpu, "Address out of bounds (SB)");
        return true;
    }
    uint8_t val = (uint8_t)(cpu->regs[GET_RT(instruction)] & 0xFF);
    cpu->ram[addr] = val;
    if (trace_enabled) {
        printf("  [%s]  mem[0x%08X] = $%d  -> 0x%02X\n",
               __func__ + 8, addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_sh(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    if (addr % 2 != 0) {
        throw_exception(cpu, "Unaligned memory access (SH)");
        return true;
    }
    if (addr > RAM_SIZE - 2) {
        throw_exception(cpu, "Address out of bounds (SH)");
        return true;
    }
    uint16_t val = (uint16_t)(cpu->regs[GET_RT(instruction)] & 0xFFFF);
    cpu->ram[addr]     = val & 0xFF;
    cpu->ram[addr + 1] = (val >> 8) & 0xFF;
    if (trace_enabled) {
        printf("  [%s]  mem[0x%08X] = $%d  -> 0x%04X\n",
               __func__ + 8, addr, GET_RT(instruction), val);
    }
    return true;
}

// BRANCHES

static bool execute_beq(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
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
    return true;
}

static bool execute_bne(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
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
    return true;
}

// JUMP

static bool execute_j(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    uint32_t target_index = instruction & 0x03FFFFFF;
    uint32_t target = ((current_pc + 4) & 0xF0000000) | (target_index << 2);
    cpu->next_pc = target;
    if (trace_enabled) {
        printf("  [%s]  Jump to 0x%08X (after delay slot)\n", __func__ + 8, target);
    }
    return true;
}

static bool execute_jal(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    uint32_t target_index = instruction & 0x03FFFFFF;
    uint32_t target = ((current_pc + 4) & 0xF0000000) | (target_index << 2);
set_reg(cpu, 31, current_pc + 8);    cpu->next_pc = target;
    if (trace_enabled) {
        printf("  [%s]  Jump and link to 0x%08X, return address stored in $31 (after delay slot)\n",
               __func__ + 8, target);
    }
    return true;
}

static bool execute_jr(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    cpu->next_pc = rs;
    if (trace_enabled) {
        printf("  [%s]  Jump register to 0x%08X (after delay slot)\n", __func__ + 8, rs);
    }
    return true;
}
static bool execute_jalr(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    uint32_t target = cpu->regs[GET_RS(instruction)];
    uint32_t rd = GET_RD(instruction);
    uint32_t return_addr = current_pc + 8;
    set_reg(cpu, rd, return_addr);
    cpu->next_pc = target;
    if (trace_enabled) {
        printf("  [%s]  -> 0x%08X, $%d=0x%08X (after delay slot)\n",
               __func__ + 8, target, rd, return_addr);
    }
    return true;
}

// FUNCTION TABLES

static const InstrFn funct_table[64] = {
    [0x00] = execute_sll,
    [0x02] = execute_srl,
    [0x03] = execute_sra,
    [0x04] = execute_sllv,
    [0x06] = execute_srlv,
    [0x07] = execute_srav,
    [0x08] = execute_jr,
    [0x09] = execute_jalr,
    [0x10] = execute_mfhi,
    [0x11] = execute_mthi,
    [0x12] = execute_mflo,
    [0x13] = execute_mtlo,
    [0x18] = execute_mult,
    [0x19] = execute_multu,
    [0x1A] = execute_div,
    [0x1B] = execute_divu,
    [0x20] = execute_add,
    [0x21] = execute_addu,
    [0x22] = execute_sub,
    [0x23] = execute_subu,
    [0x24] = execute_and,
    [0x25] = execute_or,
    [0x26] = execute_xor,
    [0x27] = execute_nor,
    [0x2A] = execute_slt,
    [0x2B] = execute_sltu,
};

static const InstrFn opcode_table[64] = {
    [0x02] = execute_j,
    [0x03] = execute_jal,
    [0x04] = execute_beq,
    [0x05] = execute_bne,
    [0x08] = execute_addi,
    [0x09] = execute_addiu,
    [0x0A] = execute_slti,
    [0x0B] = execute_sltiu,
    [0x0C] = execute_andi,
    [0x0D] = execute_ori,
    [0x0E] = execute_xori,
    [0x0F] = execute_lui,
    [0x20] = execute_lb,
    [0x21] = execute_lh,
    [0x23] = execute_lw,
    [0x24] = execute_lbu,
    [0x25] = execute_lhu,
    [0x28] = execute_sb,
    [0x29] = execute_sh,
    [0x2B] = execute_sw,
    [0x3F] = execute_hlt,
};

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

    uint32_t opcode = GET_OPCODE(instruction);
    InstrFn fn;

    if (opcode == 0x00) {
        uint32_t funct = GET_FUNCT(instruction);
        fn = funct_table[funct];
        if (!fn) {
            printf("Unknown funct: 0x%02X at PC 0x%08X\n", funct, cpu->pc);
            return false;
        }
    } else {
        fn = opcode_table[opcode];
        if (!fn) {
            printf("Unknown opcode: 0x%02X at PC 0x%08X\n", opcode, cpu->pc);
            return false;
        }
    }

    bool cont = fn(cpu, instruction, current_pc);

    if (trace_enabled) {
        dump_regs(cpu);
        printf("\n");
    }

    if (!cont) return false;
    return cpu->pc < RAM_SIZE;
}