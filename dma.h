#ifndef DMA_H
#define DMA_H

#include <stdint.h>
#include <stdbool.h>

#define DMA_NUM_CHANNELS 7

#define DMA_CH_MDEC_IN  0
#define DMA_CH_MDEC_OUT 1
#define DMA_CH_GPU      2
#define DMA_CH_CDROM    3
#define DMA_CH_SPU      4
#define DMA_CH_PIO      5
#define DMA_CH_OTC      6

typedef struct {
    uint32_t madr; 
    uint32_t bcr; 
    uint32_t chcr; 
} DMAChannel;

typedef struct {
    DMAChannel channels[DMA_NUM_CHANNELS];
    uint32_t dpcr;
    uint32_t dicr; 
} DMAController;

struct CPU;

void dma_init(DMAController *dma);

bool dma_mem_read32(struct CPU *cpu, uint32_t paddr, uint32_t *out_val);
bool dma_mem_write32(struct CPU *cpu, uint32_t paddr, uint32_t val);

#endif