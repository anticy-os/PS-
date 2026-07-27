#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

bool trace_enabled = false;

// MEMORY

uint32_t ps1_translate_address(uint32_t vaddr) {
    if (vaddr >= 0x80000000 && vaddr <= 0xBFFFFFFF) {
        return vaddr & 0x1FFFFFFF;
    }
    return vaddr;
}

uint8_t mem_read8(CPU *cpu, uint32_t addr) {
    uint32_t paddr = ps1_translate_address(addr);
    if (paddr < RAM_SIZE) return cpu->ram[paddr];
    if (paddr >= 0x1F800000 && paddr < 0x1F800000 + SCRATCHPAD_SIZE) {
        return cpu->scratchpad[paddr - 0x1F800000];
    }
    if (paddr >= 0x1FC00000 && paddr < 0x1FC00000 + BIOS_SIZE) {
        return cpu->bios[paddr - 0x1FC00000];
    }
    return 0;
}

uint16_t mem_read16(CPU *cpu, uint32_t addr) {
    return (uint16_t)mem_read8(cpu, addr) |
           ((uint16_t)mem_read8(cpu, addr + 1) << 8);
}

uint32_t mem_read32(CPU *cpu, uint32_t addr) {
    uint32_t paddr = ps1_translate_address(addr);
    if (paddr == 0x1F801810) return gpu_read_data(&cpu->gpu);
    if (paddr == 0x1F801814) return gpu_read_status(&cpu->gpu);
    return (uint32_t)mem_read8(cpu, addr) |
           ((uint32_t)mem_read8(cpu, addr + 1) << 8) |
           ((uint32_t)mem_read8(cpu, addr + 2) << 16) |
           ((uint32_t)mem_read8(cpu, addr + 3) << 24);
}

void mem_write8(CPU *cpu, uint32_t addr, uint8_t val) {
    uint32_t paddr = ps1_translate_address(addr);
    if (paddr < RAM_SIZE) {
        if (cpu->cop0[12] & (1u << 16)) {
            return;
        }
        cpu->ram[paddr] = val;
    } else if (paddr >= 0x1F800000 && paddr < 0x1F800000 + SCRATCHPAD_SIZE) {
        cpu->scratchpad[paddr - 0x1F800000] = val;
    }
}

void mem_write16(CPU *cpu, uint32_t addr, uint16_t val) {
    mem_write8(cpu, addr, (uint8_t)val);
    mem_write8(cpu, addr + 1, (uint8_t)(val >> 8));
}

void mem_write32(CPU *cpu, uint32_t addr, uint32_t val) {
    uint32_t paddr = ps1_translate_address(addr);
    if (paddr == 0x1F801810) { gpu_write_gp0(&cpu->gpu, val); return; }
    if (paddr == 0x1F801814) { gpu_write_gp1(&cpu->gpu, val); return; }
    mem_write8(cpu, addr, (uint8_t)val);
    mem_write8(cpu, addr + 1, (uint8_t)(val >> 8));
    mem_write8(cpu, addr + 2, (uint8_t)(val >> 16));
    mem_write8(cpu, addr + 3, (uint8_t)(val >> 24));
}

uint32_t cpu_read32(CPU *cpu, uint32_t addr) {
    return mem_read32(cpu, addr);
}

void cpu_write32(CPU *cpu, uint32_t addr, uint32_t val) {
    mem_write32(cpu, addr, val);
}

uint32_t load_binary(const char *filename, CPU *cpu, uint32_t load_addr) {
    uint32_t paddr = ps1_translate_address(load_addr);
    uint8_t *destination;
    size_t max_len;
    if (paddr < RAM_SIZE) {
        destination = &cpu->ram[paddr];
        max_len = RAM_SIZE - paddr;
    } else if (paddr >= 0x1FC00000 && paddr < 0x1FC00000 + BIOS_SIZE) {
        destination = &cpu->bios[paddr - 0x1FC00000];
        max_len = BIOS_SIZE - (paddr - 0x1FC00000);
    } else {
        fprintf(stderr, "load_addr out of range\n");
        return 0;
    }
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open binary file");
        return 0;
    }
    size_t bytesRead = fread(destination, 1, max_len, file);
    printf("Loaded %zu bytes into memory\n", bytesRead);
    fclose(file);
    return (uint32_t)bytesRead;
}

// ADDITIONAL FUNCTIONS

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

static void throw_exception(CPU *cpu, uint32_t current_pc, uint32_t exc_code, uint32_t bad_vaddr) {
    bool in_delay_slot = cpu->in_delay_slot;
    uint32_t status = cpu->cop0[12];

    uint32_t ku_ie = status & 0x0F;
    uint32_t new_ku_ie = (ku_ie << 2) & 0x3F;
    cpu->cop0[12] = (status & ~0x3Fu) | new_ku_ie;

    uint32_t cause = cpu->cop0[13] & ~((0x1Fu << 2) | (1u << 31));
    cpu->cop0[13] = cause | ((exc_code & 0x1F) << 2) | (in_delay_slot ? (1u << 31) : 0);
    cpu->cop0[14] = in_delay_slot ? current_pc - 4 : current_pc;

    if (exc_code == EXC_ADEL || exc_code == EXC_ADES) {
        cpu->cop0[8] = bad_vaddr;
    }

    bool bev = (status >> 22) & 1;
    uint32_t vector = bev ? 0xBFC00180 : 0x80000080;

    cpu->pc = vector;
    cpu->next_pc = vector + 4;

    if (trace_enabled) {
        printf("  [EXCEPTION] code=%u EPC=0x%08X -> vector 0x%08X\n",
               exc_code, cpu->cop0[14], vector);
    }
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

// HLT (just for testing)

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
        throw_exception(cpu, current_pc, EXC_OV, 0);
        return true;
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
        throw_exception(cpu, current_pc, EXC_OV, 0);
        return true;
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
        throw_exception(cpu, current_pc, EXC_OV, 0);
        return true;
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
        cpu->lo = (rs >= 0) ? (uint32_t)-1 : 1u;
        cpu->hi = (uint32_t)rs;
        if (trace_enabled) {
            printf("  [%s]  division by zero -> LO=0x%08X HI=0x%08X\n",
                   __func__ + 8, cpu->lo, cpu->hi);
        }
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
        cpu->lo = 0xFFFFFFFFu;
        cpu->hi = rs;
        if (trace_enabled) {
            printf("  [%s]  division by zero -> LO=0x%08X HI=0x%08X\n",
                   __func__ + 8, cpu->lo, cpu->hi);
        }
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

// COMPARISON

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
        throw_exception(cpu, current_pc, EXC_ADEL, addr);
        return true;
    }
    uint32_t val = mem_read32(cpu, addr);
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X]  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_lwl(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t addr = cpu->regs[GET_RS(instruction)] +
                    (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF);
    uint32_t offset = addr & 3;
    uint32_t mem = mem_read32(cpu, addr & ~3u);
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t val = (rt & (0x00FFFFFFu >> (offset * 8))) |
                   (mem << (24 - offset * 8));
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X] (left)  -> $%d = 0x%08X\n",
               __func__ + 8, GET_RT(instruction), addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_lwr(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t addr = cpu->regs[GET_RS(instruction)] +
                    (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF);
    uint32_t offset = addr & 3;
    uint32_t mem = mem_read32(cpu, addr & ~3u);
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t val = (rt & (0xFFFFFF00u << ((3 - offset) * 8))) |
                   (mem >> (offset * 8));
    set_reg(cpu, GET_RT(instruction), val);
    if (trace_enabled) {
        printf("  [%s]  $%d = mem[0x%08X] (right)  -> $%d = 0x%08X\n",
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
        throw_exception(cpu, current_pc, EXC_ADES, addr);
        return true;
    }
    uint32_t val = cpu->regs[GET_RT(instruction)];
    mem_write32(cpu, addr, val);
    if (trace_enabled) {
        printf("  [%s]  mem[0x%08X] = $%d  -> 0x%08X\n",
               __func__ + 8, addr, GET_RT(instruction), val);
    }
    return true;
}

static bool execute_swl(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t addr = cpu->regs[GET_RS(instruction)] +
                    (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF);
    uint32_t offset = addr & 3;
    uint32_t mem = mem_read32(cpu, addr & ~3u);
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t val = (mem & (0xFFFFFF00u << (offset * 8))) |
                   (rt >> (24 - offset * 8));
    mem_write32(cpu, addr & ~3u, val);
    if (trace_enabled) {
        printf("  [%s]  mem[0x%08X] (left) = $%d\n",
               __func__ + 8, addr, GET_RT(instruction));
    }
    return true;
}

static bool execute_swr(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t addr = cpu->regs[GET_RS(instruction)] +
                    (uint32_t)(int32_t)(int16_t)(instruction & 0xFFFF);
    uint32_t offset = addr & 3;
    uint32_t mem = mem_read32(cpu, addr & ~3u);
    uint32_t rt = cpu->regs[GET_RT(instruction)];
    uint32_t val = (mem & (0x00FFFFFFu >> ((3 - offset) * 8))) |
                   (rt << (offset * 8));
    mem_write32(cpu, addr & ~3u, val);
    if (trace_enabled) {
        printf("  [%s]  mem[0x%08X] (right) = $%d\n",
               __func__ + 8, addr, GET_RT(instruction));
    }
    return true;
}

static bool execute_lb(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)current_pc;
    uint32_t rs = cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    uint32_t addr = rs + (uint32_t)(int32_t)imm;
    int8_t byte = (int8_t)mem_read8(cpu, addr);
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
    uint32_t val = (uint32_t)mem_read8(cpu, addr);
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
        throw_exception(cpu, current_pc, EXC_ADEL, addr);
        return true;
    }
    uint16_t half = mem_read16(cpu, addr);
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
        throw_exception(cpu, current_pc, EXC_ADEL, addr);
        return true;
    }
    uint32_t val = (uint32_t)mem_read16(cpu, addr);
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
    uint8_t val = (uint8_t)(cpu->regs[GET_RT(instruction)] & 0xFF);
    mem_write8(cpu, addr, val);
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
        throw_exception(cpu, current_pc, EXC_ADES, addr);
        return true;
    }
    uint16_t val = (uint16_t)(cpu->regs[GET_RT(instruction)] & 0xFFFF);
    mem_write16(cpu, addr, val);
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
    cpu->next_in_delay_slot = true;

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
    cpu->next_in_delay_slot = true;

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
    cpu->next_in_delay_slot = true;
    if (trace_enabled) {
        printf("  [%s]  Jump to 0x%08X (after delay slot)\n", __func__ + 8, target);
    }
    return true;
}

static bool execute_jal(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    uint32_t target_index = instruction & 0x03FFFFFF;
    uint32_t target = ((current_pc + 4) & 0xF0000000) | (target_index << 2);
    set_reg(cpu, 31, current_pc + 8);
    cpu->next_pc = target;
    cpu->next_in_delay_slot = true;
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
    cpu->next_in_delay_slot = true;
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
    cpu->next_in_delay_slot = true;
    if (trace_enabled) {
        printf("  [%s]  -> 0x%08X, $%d=0x%08X (after delay slot)\n",
               __func__ + 8, target, rd, return_addr);
    }
    return true;
}

static bool execute_blez(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    cpu->next_in_delay_slot = true;

    if (rs <= 0) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d <= 0, branch taken, next_pc=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), target);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d > 0, branch not taken\n",
                   __func__ + 8, GET_RS(instruction));
        }
    }
    return true;
}

static bool execute_bltz(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    cpu->next_in_delay_slot = true;

    if (rs < 0) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d < 0, branch taken, next_pc=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), target);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d >= 0, branch not taken\n",
                   __func__ + 8, GET_RS(instruction));
        }
    }
    return true;
}

static bool execute_bgtz(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    cpu->next_in_delay_slot = true;

    if (rs > 0) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d > 0, branch taken, next_pc=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), target);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d <= 0, branch not taken\n",
                   __func__ + 8, GET_RS(instruction));
        }
    }
    return true;
}

static bool execute_bgez(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    cpu->next_in_delay_slot = true;

    if (rs >= 0) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d >= 0, branch taken, next_pc=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), target);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d < 0, branch not taken\n",
                   __func__ + 8, GET_RS(instruction));
        }
    }
    return true;
}

static bool execute_bltzal(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    cpu->next_in_delay_slot = true;

    set_reg(cpu, 31, current_pc + 8);

    if (rs < 0) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d < 0, branch taken, next_pc=0x%08X, $31=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), target, current_pc + 8);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d >= 0, branch not taken, $31=0x%08X\n",
                   __func__ + 8, GET_RS(instruction), current_pc + 8);
        }
    }
    return true;
}

static bool execute_bgezal(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    int32_t rs = (int32_t)cpu->regs[GET_RS(instruction)];
    int16_t imm = (int16_t)(instruction & 0xFFFF);
    cpu->next_in_delay_slot = true;

    set_reg(cpu, 31, current_pc + 8);

    if (rs >= 0) {
        uint32_t target = current_pc + 4 + ((uint32_t)(int32_t)imm << 2);
        cpu->next_pc = target;
        if (trace_enabled) {
            printf("  [%s]  $%d >= 0, branch taken, next_pc=0x%08X, $31=0x%08X (after delay slot)\n",
                   __func__ + 8, GET_RS(instruction), target, current_pc + 8);
        }
    } else {
        if (trace_enabled) {
            printf("  [%s]  $%d < 0, branch not taken, $31=0x%08X\n",
                   __func__ + 8, GET_RS(instruction), current_pc + 8);
        }
    }
    return true;
}

// REGIMM

static bool execute_regimm(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    uint32_t rt = GET_RT(instruction);
    switch (rt) {
        case 0x00: return execute_bltz(cpu, instruction, current_pc);
        case 0x01: return execute_bgez(cpu, instruction, current_pc);
        case 0x10: return execute_bltzal(cpu, instruction, current_pc);
        case 0x11: return execute_bgezal(cpu, instruction, current_pc);
        default:
            fprintf(stderr, "Reserved instruction: unknown REGIMM rt 0x%02X at PC 0x%08X\n", rt, current_pc);
            throw_exception(cpu, current_pc, EXC_RI, 0);
            return true;
    }
}

// COP0

static bool execute_cop0(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    uint32_t rs_field = GET_RS(instruction);
    uint32_t rt = GET_RT(instruction);
    uint32_t rd = GET_RD(instruction);

    if (rs_field == 0x00) {          // MFC0
        uint32_t val = cpu->cop0[rd];
        set_reg(cpu, rt, val);
        if (trace_enabled) {
            printf("  [MFC0] $%d = COP0[%d] = 0x%08X\n", rt, rd, val);
        }
        return true;
    }

    if (rs_field == 0x04) {          // MTC0
        uint32_t val = cpu->regs[rt];
        cpu->cop0[rd] = val;
        if (trace_enabled) {
            printf("  [MTC0] COP0[%d] = $%d = 0x%08X\n", rd, rt, val);
        }
        return true;
    }

    if (rs_field == 0x10 && GET_FUNCT(instruction) == 0x10) {  // RFE
        uint32_t status = cpu->cop0[12];
        uint32_t ku_ie_stack = status & 0x3F;
        uint32_t new_stack = (ku_ie_stack >> 2) | (ku_ie_stack & 0x30);
        cpu->cop0[12] = (status & ~0x0Fu) | (new_stack & 0x0F);
        if (trace_enabled) {
            printf("  [RFE] Status: 0x%08X -> 0x%08X\n", status, cpu->cop0[12]);
        }
        return true;
    }

    (void)current_pc;
    if (trace_enabled) {
        printf("  [COP0] unhandled rs=0x%02X\n", rs_field);
    }
    return true;
}

// SYSCALL / BREAK 

static bool execute_syscall(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)instruction;
    if (trace_enabled) {
        printf("  [syscall]  software trap\n");
    }
    throw_exception(cpu, current_pc, EXC_SYSCALL, 0);
    return true;
}

static bool execute_break(CPU *cpu, uint32_t instruction, uint32_t current_pc) {
    (void)instruction;
    if (trace_enabled) {
        printf("  [break]  breakpoint trap\n");
    }
    throw_exception(cpu, current_pc, EXC_BP, 0);
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
    [0x0C] = execute_syscall,
    [0x0D] = execute_break,
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
    [0x01] = execute_regimm,
    [0x02] = execute_j,
    [0x03] = execute_jal,
    [0x04] = execute_beq,
    [0x05] = execute_bne,
    [0x06] = execute_blez,
    [0x07] = execute_bgtz,
    [0x08] = execute_addi,
    [0x09] = execute_addiu,
    [0x10] = execute_cop0,
    [0x0A] = execute_slti,
    [0x0B] = execute_sltiu,
    [0x0C] = execute_andi,
    [0x0D] = execute_ori,
    [0x0E] = execute_xori,
    [0x0F] = execute_lui,
    [0x20] = execute_lb,
    [0x21] = execute_lh,
    [0x22] = execute_lwl,
    [0x23] = execute_lw,
    [0x24] = execute_lbu,
    [0x25] = execute_lhu,
    [0x26] = execute_lwr,
    [0x28] = execute_sb,
    [0x29] = execute_sh,
    [0x2A] = execute_swl,
    [0x2B] = execute_sw,
    [0x2E] = execute_swr,
    [0x3F] = execute_hlt,
};

// MAIN

bool cpu_step(CPU *cpu) {
    cpu->in_delay_slot = cpu->next_in_delay_slot;
    cpu->next_in_delay_slot = false;

    uint32_t current_pc = cpu->pc;
    uint32_t instruction = cpu_read32(cpu, current_pc);
    cpu->pc = cpu->next_pc;
    cpu->next_pc = cpu->pc + 4;

    if (trace_enabled) {
        printf("PC: 0x%08X | Instr: 0x%08X\n", current_pc, instruction);
    }

    uint32_t opcode = GET_OPCODE(instruction);
    InstrFn fn;

    if (opcode == 0x00) {
        uint32_t funct = GET_FUNCT(instruction);
        fn = funct_table[funct];
        if (!fn) {
            fprintf(stderr, "Reserved instruction: unknown funct 0x%02X at PC 0x%08X\n", funct, current_pc);
            throw_exception(cpu, current_pc, EXC_RI, 0);
            fn = NULL;
        }
    } else {
        fn = opcode_table[opcode];
        if (!fn) {
            fprintf(stderr, "Reserved instruction: unknown opcode 0x%02X at PC 0x%08X\n", opcode, current_pc);
            throw_exception(cpu, current_pc, EXC_RI, 0);
            fn = NULL;
        }
    }

    if (trace_enabled) {
            dump_regs(cpu);
            printf("\n");
    }
    return true;
}