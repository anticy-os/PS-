#ifndef GPU_H
#define GPU_H

#include <stdint.h>
#include <stdbool.h>

#define VRAM_WIDTH  1024
#define VRAM_HEIGHT 512

typedef struct {
    uint16_t vram[VRAM_WIDTH * VRAM_HEIGHT];

    uint32_t gpustat;
    uint32_t gp0_cmd;
    uint32_t gp0_args[16];
    int      gp0_args_needed;
    int      gp0_args_have;

    bool     transfer_active;
    uint16_t transfer_x, transfer_y;
    uint16_t transfer_w, transfer_h;
    uint16_t transfer_row, transfer_col;

    bool display_enabled;
} GPU;

void gpu_init(GPU *gpu);
void gpu_write_gp0(GPU *gpu, uint32_t value);
void gpu_write_gp1(GPU *gpu, uint32_t value);
uint32_t gpu_read_status(GPU *gpu);
uint32_t gpu_read_data(GPU *gpu);

#endif