CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)

ps1emu: main.c cpu.c cpu.h gpu.c gpu.h video.c video.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) main.c cpu.c gpu.c video.c -o $@ $(SDL_LIBS)

.PHONY: clean
clean:
	rm -f ps1emu
