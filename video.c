#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_MAX_WIDTH 640
#define VIDEO_MAX_HEIGHT 480
#define FRAME_INTERVAL_MS 16

static int display_width(const GPU *gpu) {
    switch ((gpu->display_mode & 3) | ((gpu->display_mode & 0x40) >> 4)) {
        case 0: return 256;
        case 1: return 320;
        case 2: return 512;
        case 3: return 640;
        case 4: return 368;
        default: return 320;
    }
}

static int display_height(const GPU *gpu) {
    int height = (int)gpu->display_v_end - gpu->display_v_start;
    if (height <= 0 || height > VRAM_HEIGHT) return 240;
    return height;
}

static uint32_t bgr555_to_argb8888(uint16_t pixel) {
    uint32_t r = (pixel & 0x1F) << 3;
    uint32_t g = ((pixel >> 5) & 0x1F) << 3;
    uint32_t b = ((pixel >> 10) & 0x1F) << 3;
    r |= r >> 5;
    g |= g >> 5;
    b |= b >> 5;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static bool resize_output(Video *video, int width, int height) {
    if (video->texture && video->width == width && video->height == height) return true;

    SDL_DestroyTexture(video->texture);
    video->texture = SDL_CreateTexture(video->renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!video->texture) {
        fprintf(stderr, "Could not create video texture: %s\n", SDL_GetError());
        return false;
    }
    video->width = width;
    video->height = height;
    SDL_SetWindowSize(video->window, width * 2, height * 2);
    return true;
}

bool video_init(Video *video) {
    memset(video, 0, sizeof(*video));
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Could not initialise SDL video: %s\n", SDL_GetError());
        return false;
    }

    video->window = SDL_CreateWindow("PS! Emulator", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, 640, 480,
                                     SDL_WINDOW_RESIZABLE);
    if (!video->window) goto fail;
    video->renderer = SDL_CreateRenderer(video->window, -1,
                                         SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!video->renderer) goto fail;
    video->pixels = malloc(VIDEO_MAX_WIDTH * VIDEO_MAX_HEIGHT * sizeof(*video->pixels));
    if (!video->pixels) {
        fprintf(stderr, "Could not allocate video framebuffer\n");
        goto fail;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    return true;

fail:
    fprintf(stderr, "Could not initialise video output: %s\n", SDL_GetError());
    video_destroy(video);
    return false;
}

bool video_update(Video *video, const GPU *gpu) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) return false;
    }

    uint32_t now = SDL_GetTicks();
    if (now - video->last_present_ms < FRAME_INTERVAL_MS) return true;
    video->last_present_ms = now;

    int width = display_width(gpu);
    int height = display_height(gpu);
    if (width > VIDEO_MAX_WIDTH || height > VIDEO_MAX_HEIGHT || !resize_output(video, width, height)) {
        return false;
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t pixel = gpu->display_enabled
                ? gpu->vram[((gpu->display_y + y) % VRAM_HEIGHT) * VRAM_WIDTH +
                            ((gpu->display_x + x) % VRAM_WIDTH)]
                : 0;
            video->pixels[y * width + x] = bgr555_to_argb8888(pixel);
        }
    }

    SDL_UpdateTexture(video->texture, NULL, video->pixels, width * (int)sizeof(*video->pixels));
    SDL_RenderClear(video->renderer);
    SDL_RenderCopy(video->renderer, video->texture, NULL, NULL);
    SDL_RenderPresent(video->renderer);
    return true;
}

void video_destroy(Video *video) {
    if (!video) return;
    SDL_DestroyTexture(video->texture);
    SDL_DestroyRenderer(video->renderer);
    SDL_DestroyWindow(video->window);
    free(video->pixels);
    SDL_Quit();
    memset(video, 0, sizeof(*video));
}
