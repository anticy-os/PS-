
#include "gpu.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

extern bool trace_enabled;

void gpu_init(GPU *gpu){
    memset(gpu, 0, sizeof(GPU));
    gpu->gpustat = 0x1C000000;
    gpu->display_h_start = 0x260;
    gpu->display_h_end = 0xC60;
    gpu->display_v_start = 0x10;
    gpu->display_v_end = 0x100;
}

static void gpu_fill_rect(GPU *gpu, uint32_t color_word, uint32_t pos, uint32_t size){
    uint8_t r = color_word & 0xFF;
    uint8_t g = (color_word >> 8) & 0xFF;
    uint8_t b = (color_word >> 16) & 0xFF;
    uint16_t color15 = ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3);

    uint16_t x = pos & 0x3F0;
    uint16_t y = (pos >> 16) & 0x1FF;
    uint16_t w = ((size & 0x3FF) + 0xF) & ~0xF;
    uint16_t h = (size >> 16) & 0x1FF;

    if(trace_enabled){
        printf("  [GPU] Fill rect x=%u y=%u w=%u h=%u color=0x%06X\n",
                x, y, w, h, color_word & 0xFFFFFF);
    }

    for(uint16_t row = 0; row < h; row++){
        uint16_t vy = (y + row) % VRAM_HEIGHT;
        for(uint16_t col = 0; col < w; col++){
            uint16_t vx = (x + col) % VRAM_WIDTH;
            gpu->vram[vy * VRAM_WIDTH + vx] = color15;
        }
    }
}

static void gpu_start_cpu_to_vram(GPU *gpu, uint32_t pos, uint32_t size) {
    gpu->transfer_x = pos & 0x3FF;
    gpu->transfer_y = (pos >> 16) & 0x1FF;
    gpu->transfer_w = size & 0x3FF;
    gpu->transfer_h = (size >> 16) & 0x1FF;
    gpu->transfer_row = 0;
    gpu->transfer_col = 0;
    gpu->transfer_active = (gpu->transfer_w > 0 && gpu->transfer_h > 0);

    if (trace_enabled) {
        printf("  [GPU] Begin CPU->VRAM copy dst=(%u,%u) size=%ux%u\n",
               gpu->transfer_x, gpu->transfer_y, gpu->transfer_w, gpu->transfer_h);
    }
}

static void gpu_feed_transfer(GPU *gpu, uint32_t data) {
    uint16_t pixels[2] = { (uint16_t)data, (uint16_t)(data >> 16) };

    for (int i = 0; i < 2 && gpu->transfer_active; i++) {
        uint16_t vx = (gpu->transfer_x + gpu->transfer_col) % VRAM_WIDTH;
        uint16_t vy = (gpu->transfer_y + gpu->transfer_row) % VRAM_HEIGHT;
        gpu->vram[vy * VRAM_WIDTH + vx] = pixels[i];

        gpu->transfer_col++;
        if (gpu->transfer_col >= gpu->transfer_w) {
            gpu->transfer_col = 0;
            gpu->transfer_row++;
            if (gpu->transfer_row >= gpu->transfer_h) {
                gpu->transfer_active = false;
                if (trace_enabled) {
                    printf("  [GPU] CPU->VRAM copy complete\n");
                }
            }
        }
    }
}

void gpu_write_gp0(GPU *gpu, uint32_t value) {
    if (gpu->transfer_active) {
        gpu_feed_transfer(gpu, value);
        return;
    }

    if (gpu->gp0_args_needed > 0) {
        gpu->gp0_args[gpu->gp0_args_have++] = value;
        if (gpu->gp0_args_have < gpu->gp0_args_needed) return;

        uint8_t cmd = (gpu->gp0_cmd >> 24) & 0xFF;
        switch (cmd) {
            case 0x02: // FILL RECT
                gpu_fill_rect(gpu, gpu->gp0_cmd, gpu->gp0_args[0], gpu->gp0_args[1]);
                break;
            case 0xA0: // COPY RECT
                gpu_start_cpu_to_vram(gpu, gpu->gp0_args[0], gpu->gp0_args[1]);
                break;
            default:
                break;
        }
        gpu->gp0_args_needed = 0;
        gpu->gp0_args_have = 0;
        return;
    }

    uint8_t cmd = (value >> 24) & 0xFF;
    switch (cmd) {
        case 0x00: // NOP
            break;
        case 0x01: // CLEAR CACHE
            break;
        case 0x02: // FILL RECT
            gpu->gp0_cmd = value;
            gpu->gp0_args_needed = 2;
            gpu->gp0_args_have = 0;
            break;
        case 0xA0: // COPY RECT
            gpu->gp0_cmd = value;
            gpu->gp0_args_needed = 2;
            gpu->gp0_args_have = 0;
            break;
        case 0xE1: case 0xE2: case 0xE3:
        case 0xE4: case 0xE5: case 0xE6:
            if (trace_enabled) {
                printf("  [GPU] GP0 setting 0x%02X = 0x%08X (ignored)\n", cmd, value);
            }
            break;
        default:
            if (trace_enabled) {
                printf("  [GPU] Unhandled GP0 command 0x%02X (word=0x%08X)\n", cmd, value);
            }
            break;
    }
}

void gpu_write_gp1(GPU *gpu, uint32_t value) {
    uint8_t cmd = (value >> 24) & 0xFF;
    switch (cmd) {
        case 0x00: // RESET
            gpu_init(gpu);
            if (trace_enabled) printf("  [GPU] Reset\n");
            break;
        case 0x03: // ENABLE/DISABLE
            gpu->display_enabled = !(value & 1);
            if (trace_enabled) {
                printf("  [GPU] Display %s\n", gpu->display_enabled ? "enabled" : "disabled");
            }
            break;
        case 0x05: // DISPLAY VRAM START
            gpu->display_x = value & 0x3FF;
            gpu->display_y = (value >> 10) & 0x1FF;
            break;
        case 0x06: // HORIZONTAL DISPLAY RANGE
            gpu->display_h_start = value & 0xFFF;
            gpu->display_h_end = (value >> 12) & 0xFFF;
            break;
        case 0x07: // VERTICAL DISPLAY RANGE
            gpu->display_v_start = value & 0x3FF;
            gpu->display_v_end = (value >> 10) & 0x3FF;
            break;
        case 0x08: // DISPLAY MODE
            gpu->display_mode = value & 0x7F;
            break;
        default:
            if (trace_enabled) {
                printf("  [GPU] Unhandled GP1 command 0x%02X (word=0x%08X)\n", cmd, value);
            }
            break;
    }
}

uint32_t gpu_read_status(GPU *gpu) {
    return gpu->gpustat | 0x1C000000;
}

uint32_t gpu_read_data(GPU *gpu) {
    (void)gpu;
    return 0;
}
