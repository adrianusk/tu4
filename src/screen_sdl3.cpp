/*
 * screen_sdl3.cpp - Text-mode display backend using SDL3
 *
 * Creates a window, renders the 80x50 text buffer using CP437 8x8 font
 * and EGA 16-color palette.
 */

#include <SDL3/SDL.h>
#include <cstring>
#include "settings.h"
#include "screen.h"
#include "tu4.h"

/* CP437 8x8 font data */
#include "../cp437_8x8.h"

extern bool verbose;

/* EGA 16-color palette */
static uint32_t egaPalette[16] = {
    0x000000,  /*  0 Black */
    0x0000AA,  /*  1 Blue */
    0x00AA00,  /*  2 Green */
    0x00AAAA,  /*  3 Cyan */
    0xAA0000,  /*  4 Red */
    0xAA00AA,  /*  5 Magenta */
    0xAA5500,  /*  6 Brown */
    0xAAAAAA,  /*  7 Light Gray */
    0x555555,  /*  8 Dark Gray */
    0x5555FF,  /*  9 Light Blue */
    0x55FF55,  /* 10 Light Green */
    0x55FFFF,  /* 11 Light Cyan */
    0xFF5555,  /* 12 Light Red */
    0xFF55FF,  /* 13 Light Magenta */
    0xFFFF55,  /* 14 Yellow */
    0xFFFFFF   /* 15 White */
};

/* Character grid comes from screen.h (single source of truth). */
#define COLS       SCREEN_COLS
#define ROWS       SCREEN_ROWS
/* Pixel size of one glyph cell in the font bitmap (not the tile size). */
#define CHAR_W     8
#define CHAR_H     8
/* Logical (pre-scale) window size in pixels = grid * glyph-cell. */
#define LOGICAL_W  (COLS * CHAR_W)
#define LOGICAL_H  (ROWS * CHAR_H)

static SDL_Window*   sdlWindow   = NULL;
static SDL_Renderer* sdlRenderer = NULL;
static SDL_Texture*  sdlTexture  = NULL;
static uint32_t      framebuffer[LOGICAL_W * LOGICAL_H];
static int           displayScale = 1;


/**
 * Render the 80x50 text buffer into the pixel framebuffer.
 */
static void renderTextBuffer() {
    const uint8_t* textBuf = screenGetBuffer();

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            int cellIdx = (row * COLS + col) * 2;
            uint8_t ch   = textBuf[cellIdx];
            uint8_t attr = textBuf[cellIdx + 1];

            uint32_t fgColor = egaPalette[attr & 0x0F];
            uint32_t bgColor = egaPalette[(attr >> 4) & 0x0F];

            int px = col * CHAR_W;
            int py = row * CHAR_H;

            for (int y = 0; y < CHAR_H; y++) {
                uint8_t bits = cp437_font[ch * 8 + y];
                int offset = (py + y) * LOGICAL_W + px;
                for (int x = 0; x < CHAR_W; x++) {
                    framebuffer[offset + x] = (bits & (0x80 >> x)) ? fgColor : bgColor;
                }
            }
        }
    }
}


/*-----------------------------------------------------------------------
 * System interface (called by screen.cpp)
 *-----------------------------------------------------------------------*/

void screenInit_sys(const Settings* settings, int* dim, int reset) {
    if (!reset) {
        if (!SDL_WasInit(SDL_INIT_VIDEO)) {
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
                return;
            }
        }

        displayScale = settings->scale;
        if (displayScale < 1) displayScale = 1;
        if (displayScale > 5) displayScale = 5;

        int winW = LOGICAL_W * displayScale;
        int winH = LOGICAL_H * displayScale;

        SDL_WindowFlags flags = 0;
        if (settings->fullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;

        sdlWindow = SDL_CreateWindow("tu4 - Ultima IV Text Mode", winW, winH, flags);
        if (!sdlWindow) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return;
        }

        sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
        if (!sdlRenderer) {
            fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return;
        }

        SDL_SetRenderLogicalPresentation(sdlRenderer, LOGICAL_W, LOGICAL_H,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);

        sdlTexture = SDL_CreateTexture(sdlRenderer,
                        SDL_PIXELFORMAT_XRGB8888,
                        SDL_TEXTUREACCESS_STREAMING,
                        LOGICAL_W, LOGICAL_H);
        if (sdlTexture) {
            SDL_SetTextureScaleMode(sdlTexture, SDL_SCALEMODE_NEAREST);
        }

        memset(framebuffer, 0, sizeof(framebuffer));

        if (verbose) {
            printf("SDL3 Display: %dx%d (scale %d)\n", winW, winH, displayScale);
        }
    } else {
        /* Reset — resize window */
        displayScale = settings->scale;
        if (displayScale < 1) displayScale = 1;
        if (displayScale > 5) displayScale = 5;

        if (sdlWindow) {
            SDL_SetWindowSize(sdlWindow, LOGICAL_W * displayScale, LOGICAL_H * displayScale);
        }
    }

    dim[0] = 0;
    dim[1] = 0;
    dim[2] = LOGICAL_W;
    dim[3] = LOGICAL_H;
}

void screenDelete_sys() {
    if (sdlTexture) {
        SDL_DestroyTexture(sdlTexture);
        sdlTexture = NULL;
    }
    if (sdlRenderer) {
        SDL_DestroyRenderer(sdlRenderer);
        sdlRenderer = NULL;
    }
    if (sdlWindow) {
        SDL_DestroyWindow(sdlWindow);
        sdlWindow = NULL;
    }
}

/**
 * Render the text buffer to the display.
 */
void screenSwapBuffers() {
    if (!sdlTexture || !sdlRenderer)
        return;

    renderTextBuffer();

    SDL_UpdateTexture(sdlTexture, NULL, framebuffer, LOGICAL_W * sizeof(uint32_t));
    SDL_RenderClear(sdlRenderer);
    SDL_RenderTexture(sdlRenderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent(sdlRenderer);
}

void screenWait(int numberOfAnimationFrames) {
    SDL_Delay(numberOfAnimationFrames * (1000 / 24));
}

void screenIconify() {
    if (sdlWindow)
        SDL_MinimizeWindow(sdlWindow);
}

void screenSetMouseCursor(MouseCursor cursor) {
    (void)cursor;
}

void screenShowMouseCursor(bool visible) {
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

/**
 * Return the SDL window (for event_sdl3.cpp to access).
 */
SDL_Window* screenGetWindow() {
    return sdlWindow;
}
