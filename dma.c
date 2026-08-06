#include "dma.h"
#include "cpu.h"
#include <stdio.h>
#include <string.h>

extern bool trace_enabled;

#define DMA_BASE 0x1F801080
#define DMA_DPCR 0x1F8010F0
#define DMA_DICR 0x1F8010F4

void dma_init(DMAController *dma) {
    memset(dma, 0, sizeof(*dma));
    dma->dpcr = 0x07654321; // default priority
}

static uint32_t ram_read32(CPU *cpu, uint32_t addr) {
    addr &= (RAM_SIZE - 1);
    return  (uint32_t)cpu->ram[addr] |
            ((uint32_t)cpu->ram[addr + 1] << 8) |
            ((uint32_t)cpu->ram[addr + 2] << 16) |
            ((uint32_t)cpu->ram[addr + 3] << 24);
}

static void ram_write32(CPU *cpu, uint32_t addr, uint32_t val) {
    addr &= (RAM_SIZE - 1);
    cpu->ram[addr]     = (uint8_t)(val);
    cpu->ram[addr + 1] = (uint8_t)(val >> 8);
    cpu->ram[addr + 2] = (uint8_t)(val >> 16);
    cpu->ram[addr + 3] = (uint8_t)(val >> 24);
}

static void dma_run_gpu_channel(CPU *cpu, DMAChannel *ch){
     uint32_t direction = ch->chcr & 1;
     uint32_t sync_mode = (ch->chcr >> 9) & 3;

     if (trace_enabled) {
          printf(" [DMA2/GPU] start madr=0x%08X bcr=0x%08X chcr=0x%08X sync=%u dir=%s\n",
                    ch->madr, ch->bcr, ch->chcr, sync_mode, direction ? "RAM->GPU" : "GPU->RAM");
     }

     if(sync_mode == 2){
          uint32_t addr = ch->madr & 0x1FFFFC;
          int safety = 1 << 20;
          while(addr != 0xFFFFFF && safety-- > 0){
               uint32_t header = ram_read32(cpu, addr);
               uint32_t word_count = header >> 24;
               uint32_t next_addr = header & 0x00FFFFFF;
               
               for(uint32_t i = 0; i < word_count; i++){
                    uint32_t data = ram_read32(cpu, addr + 4 + i * 4);
                    gpu_write_gp0(&cpu->gpu, data);
               }

               if (next_addr == 0x00FFFFFF) break;
               addr = next_addr & 0x1FFFFC;
          }
     } else {
        uint32_t word_count;
        if (sync_mode == 1) {
            uint32_t block_size  = ch->bcr & 0xFFFF;
            uint32_t block_count = (ch->bcr >> 16) & 0xFFFF;
            word_count = block_size * block_count;
        } else {
            word_count = ch->bcr & 0xFFFF;
            if (word_count == 0) word_count = 0x10000; 
        }
 
        uint32_t addr = ch->madr & 0x1FFFFC;
        int32_t step = (ch->chcr & (1 << 1)) ? -4 : 4; 
        for (uint32_t i = 0; i < word_count; i++) {
            if (direction) {
                uint32_t word = ram_read32(cpu, addr);
                gpu_write_gp0(&cpu->gpu, word);
            } else {
                uint32_t word = gpu_read_data(&cpu->gpu);
                ram_write32(cpu, addr, word);
            }
            addr = (uint32_t)((int64_t)addr + step);
          }
     
          if (trace_enabled) {
               printf(" [DMA2/GPU] transfer complete\n");
          }
    }
}

static void dma_run_otc_channel(CPU *cpu, DMAChannel *ch) {
    uint32_t count = ch->bcr & 0xFFFF;
    if (count == 0) count = 0x10000;
 
    uint32_t addr = ch->madr & 0x1FFFFC;
    for (uint32_t i = 0; i < count; i++) {
        if (i == count - 1) {
            ram_write32(cpu, addr, 0x00FFFFFF); 
        } else {
            uint32_t prev = (addr - 4) & 0x1FFFFC;
            ram_write32(cpu, addr, prev);
        }
        addr = (addr + 4) & 0x1FFFFC;
    }
 
    if (trace_enabled) {
        printf(" [DMA6/OTC] cleared %u entries starting at 0x%08X\n", count, ch->madr);
    }
}
 
static void dma_trigger(CPU *cpu, int channel_index) {
    DMAChannel *ch = &cpu->dma.channels[channel_index];
 
    bool start = (ch->chcr & (1u << 24)) != 0;
    if (!start) return;
 
    switch (channel_index) {
        case DMA_CH_GPU:
            dma_run_gpu_channel(cpu, ch);
            break;
        case DMA_CH_OTC:
            dma_run_otc_channel(cpu, ch);
            break;
        default:
            if (trace_enabled) {
                printf(" [DMA%d] start ignored (unknown channel)\n", channel_index);
            }
            break;
    }
 
    ch->chcr &= ~((1u << 24) | (1u << 28));
}
 
bool dma_mem_read32(struct CPU *cpu, uint32_t paddr, uint32_t *out_val) {
    if (paddr == DMA_DPCR) { *out_val = cpu->dma.dpcr; return true; }
    if (paddr == DMA_DICR) { *out_val = cpu->dma.dicr; return true; }
 
    if (paddr >= DMA_BASE && paddr < DMA_BASE + DMA_NUM_CHANNELS * 0x10) {
        int ch = (paddr - DMA_BASE) / 0x10;
        int reg = (paddr - DMA_BASE) % 0x10;
        switch (reg) {
            case 0x0: *out_val = cpu->dma.channels[ch].madr; return true;
            case 0x4: *out_val = cpu->dma.channels[ch].bcr;  return true;
            case 0x8: *out_val = cpu->dma.channels[ch].chcr; return true;
        }
    }
    return false;
}
 
bool dma_mem_write32(struct CPU *cpu, uint32_t paddr, uint32_t val) {
    if (paddr == DMA_DPCR) { cpu->dma.dpcr = val; return true; }
    if (paddr == DMA_DICR) { cpu->dma.dicr = val; return true; }
 
    if (paddr >= DMA_BASE && paddr < DMA_BASE + DMA_NUM_CHANNELS * 0x10) {
        int ch = (paddr - DMA_BASE) / 0x10;
        int reg = (paddr - DMA_BASE) % 0x10;
        switch (reg) {
            case 0x0: cpu->dma.channels[ch].madr = val & 0x00FFFFFF; return true;
            case 0x4: cpu->dma.channels[ch].bcr  = val; return true;
            case 0x8:
                cpu->dma.channels[ch].chcr = val;
                dma_trigger(cpu, ch);
                return true;
        }
    }
    return false;
}

