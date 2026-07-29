#ifndef VIDEO_H
#define VIDEO_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

#include "gpu.h"

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t *pixels;
    int width;
    int height;
    uint32_t last_present_ms;
} Video;

bool video_init(Video *video);
bool video_update(Video *video, const GPU *gpu);
void video_destroy(Video *video);

#endif
