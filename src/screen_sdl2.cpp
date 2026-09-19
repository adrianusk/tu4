/*
 * screen_sdl2.cpp - Text-mode display backend using SDL2
 *
 * Creates a window, renders the 80x50 text buffer using CP437 8x8 font
 * and EGA 16-color palette. Pattern taken directly from introt.c.
 */

#include <SDL2/SDL.h>
#include <cstring>
#include "settings.h"
#include "screen.h"
#include "u4file.h"
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

/*
 * Glyph font (8x8, 256 chars, MSB-first per row = same bit order the
 * renderer uses: bit 0x80 is the leftmost pixel).
 *
 * By default the renderer uses the compiled-in cp437_font[] (cp437_8x8.h).
 * If the active theme ships a font at graphics/<textStyle>/cp437_8x8.bin
 * (exactly FONT_BYTES), it is loaded into themeFont[] and used instead;
 * this lets a theme supply an alternate glyph set (e.g. EDSCII). If the
 * file is absent or the wrong size, we fall back to the compiled font.
 * activeFont always points at whichever is in effect.
 */
#define FONT_CHARS  256
#define FONT_BYTES  (FONT_CHARS * CHAR_H)   /* 2048 */
static uint8_t        themeFont[FONT_BYTES];
static const uint8_t* activeFont = cp437_font;

/*
 * Load the glyph font for the given text style from
 * graphics/<textStyle>/cp437_8x8.bin. On success point activeFont at the
 * loaded themeFont[]; on any failure fall back to the compiled cp437_font[].
 * Safe to call repeatedly (e.g. on theme change).
 */
static void loadThemeFont(const char* textStyle) {
    activeFont = cp437_font;   /* default / fallback */

    if (textStyle && *textStyle) {
        std::string rel = std::string(textStyle) + "/cp437_8x8.bin";
        std::string path = u4find_graphics(rel);
        if (!path.empty()) {
            U4FILE* f = u4fopen_stdio(path.c_str());
            if (f) {
                if (u4flength(f) == FONT_BYTES &&
                    u4fread(themeFont, 1, FONT_BYTES, f) == (size_t)FONT_BYTES) {
                    activeFont = themeFont;
                    if (verbose)
                        printf("Loaded theme glyph font: %s\n", path.c_str());
                }
                u4fclose(f);
            }
        }
    }

    if (activeFont == cp437_font && verbose)
        printf("Using compiled cp437 glyph font (theme '%s' has no usable "
               "cp437_8x8.bin)\n", textStyle ? textStyle : "");
}

/*
 * Colour palette (16 entries, 0x00RRGGBB). By default the renderer uses
 * the compiled-in egaPalette[]. If the active theme ships a palette at
 * graphics/<textStyle>/palette.bin (exactly PAL_BYTES = 16*3 RGB bytes in
 * EGA index order), it is loaded into themePalette[] and used instead;
 * this lets a theme supply an alternate palette (e.g. C64 Pepto). If the
 * file is absent or the wrong size, we fall back to the compiled palette.
 * activePalette always points at whichever is in effect.
 */
#define PAL_COLORS  16
#define PAL_BYTES   (PAL_COLORS * 3)   /* 48 */
static uint32_t        themePalette[PAL_COLORS];
static const uint32_t* activePalette = egaPalette;

static void loadThemePalette(const char* textStyle) {
    activePalette = egaPalette;   /* default / fallback */

    if (textStyle && *textStyle) {
        std::string rel = std::string(textStyle) + "/palette.bin";
        std::string path = u4find_graphics(rel);
        if (!path.empty()) {
            U4FILE* f = u4fopen_stdio(path.c_str());
            if (f) {
                uint8_t rgb[PAL_BYTES];
                if (u4flength(f) == PAL_BYTES &&
                    u4fread(rgb, 1, PAL_BYTES, f) == (size_t)PAL_BYTES) {
                    for (int i = 0; i < PAL_COLORS; i++) {
                        themePalette[i] = ((uint32_t)rgb[i*3]   << 16) |
                                          ((uint32_t)rgb[i*3+1] << 8)  |
                                          ((uint32_t)rgb[i*3+2]);
                    }
                    activePalette = themePalette;
                    if (verbose)
                        printf("Loaded theme palette: %s\n", path.c_str());
                }
                u4fclose(f);
            }
        }
    }

    if (activePalette == egaPalette && verbose)
        printf("Using compiled EGA palette (theme '%s' has no usable "
               "palette.bin)\n", textStyle ? textStyle : "");
}

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
    int vertOffset = screenState()->vertOffset;  // in character rows

    // If vertOffset, fill top rows with black
    if (vertOffset > 0) {
        int blackPixels = vertOffset * CHAR_H * LOGICAL_W;
        memset(framebuffer, 0, blackPixels * sizeof(uint32_t));
    }

    for (int row = 0; row < ROWS; row++) {
        int dstRow = row + vertOffset;
        if (dstRow >= ROWS) break;

        for (int col = 0; col < COLS; col++) {
            int cellIdx = (row * COLS + col) * 2;
            uint8_t ch   = textBuf[cellIdx];
            uint8_t attr = textBuf[cellIdx + 1];

            uint32_t fgColor = activePalette[attr & 0x0F];
            uint32_t bgColor = activePalette[(attr >> 4) & 0x0F];

            int px = col * CHAR_W;
            int py = dstRow * CHAR_H;

            for (int y = 0; y < CHAR_H; y++) {
                uint8_t bits = activeFont[ch * 8 + y];
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
    /* Pick up the active theme's glyph font (falls back to the compiled
       cp437_font if the theme ships no cp437_8x8.bin). Runs on both first
       init and on theme-change reinit (reset=1), so switching Text Style
       reloads the font. */
    loadThemeFont(settings ? settings->textStyle.c_str() : NULL);
    /* Likewise pick up the active theme's colour palette (falls back to
       the compiled egaPalette if the theme ships no palette.bin). */
    loadThemePalette(settings ? settings->textStyle.c_str() : NULL);

    if (!reset) {
        if (!SDL_WasInit(SDL_INIT_VIDEO)) {
            if (SDL_Init(SDL_INIT_VIDEO) < 0) {
                fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
                return;
            }
        }

        displayScale = settings->scale;
        if (displayScale < 1) displayScale = 1;
        if (displayScale > 5) displayScale = 5;

        int winW = LOGICAL_W * displayScale;
        int winH = LOGICAL_H * displayScale;

        Uint32 flags = SDL_WINDOW_SHOWN;
        if (settings->fullscreen)
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

        sdlWindow = SDL_CreateWindow("tu4 - Ultima IV Text Mode",
                        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                        winW, winH, flags);
        if (!sdlWindow) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return;
        }

        /* Window / taskbar icon: avatar tile rendered to graphics/tu4.bmp
           (solid black background). Core SDL only (SDL_LoadBMP) so no
           SDL_image dependency.
           NOTE: convert to a known 32-bit RGBA surface with a fully-opaque
           alpha before SDL_SetWindowIcon. A 24-bit BMP handed straight to
           the window manager can end up with an undefined/zero alpha in
           _NET_WM_ICON and render as fully transparent (invisible) on some
           WMs (e.g. Cinnamon/Muffin). */
        {
            SDL_Surface* raw = SDL_LoadBMP("graphics/tu4.bmp");
            if (raw) {
                SDL_Surface* icon =
                    SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
                SDL_FreeSurface(raw);
                if (icon) {
                    /* Force every pixel fully opaque (A=255). */
                    SDL_SetSurfaceBlendMode(icon, SDL_BLENDMODE_NONE);
                    if (SDL_LockSurface(icon) == 0) {
                        uint8_t* p = (uint8_t*)icon->pixels;
                        for (int y = 0; y < icon->h; ++y) {
                            uint8_t* row = p + y * icon->pitch;
                            for (int x = 0; x < icon->w; ++x)
                                row[x * 4 + 3] = 0xFF;   /* A last in RGBA32 */
                        }
                        SDL_UnlockSurface(icon);
                    }
                    SDL_SetWindowIcon(sdlWindow, icon);
                    SDL_FreeSurface(icon);
                }
            } else if (verbose) {
                fprintf(stderr, "window icon load failed: %s\n",
                        SDL_GetError());
            }
        }

        sdlRenderer = SDL_CreateRenderer(sdlWindow, -1,
                        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!sdlRenderer) {
            sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_SOFTWARE);
        }

        SDL_RenderSetLogicalSize(sdlRenderer, LOGICAL_W, LOGICAL_H);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

        sdlTexture = SDL_CreateTexture(sdlRenderer,
                        SDL_PIXELFORMAT_RGB888,
                        SDL_TEXTUREACCESS_STREAMING,
                        LOGICAL_W, LOGICAL_H);

        memset(framebuffer, 0, sizeof(framebuffer));

        if (verbose) {
            printf("SDL2 Display: %dx%d (scale %d)\n", winW, winH, displayScale);
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

/* Mouse cursor state (used by screenSwapBuffers and screenDrawMouseCursor) */
static MouseCursor storedMouseCursor = MC_DEFAULT;
static int mouseCursorX = -1;
static int mouseCursorY = -1;

/**
 * Render the text buffer to the display.
 */
void screenSwapBuffers() {
    if (!sdlTexture || !sdlRenderer)
        return;

    /* Temporarily draw mouse cursor into text buffer for this frame */
    bool cursorDrawn = false;
    uint8_t savedCh = ' ', savedAttr = 0x00;
    if (mouseCursorX >= 0 && mouseCursorY >= 0) {
        static const uint8_t arrowChars[] = {
            0x02, 0x1B, 0x18, 0x1A, 0x19
        };
        screenGetChar(mouseCursorX, mouseCursorY, &savedCh, &savedAttr);
        MouseCursor cur = storedMouseCursor;
        uint8_t arrow = arrowChars[cur < 5 ? cur : 0];
        screenPutChar(mouseCursorX, mouseCursorY, arrow, 0x0F);
        cursorDrawn = true;
    }

    renderTextBuffer();

    /* Restore the text buffer after rendering */
    if (cursorDrawn) {
        screenPutChar(mouseCursorX, mouseCursorY, savedCh, savedAttr);
    }

    SDL_UpdateTexture(sdlTexture, NULL, framebuffer, LOGICAL_W * sizeof(uint32_t));
    SDL_RenderClear(sdlRenderer);
    SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
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
    static MouseCursor currentCursor = MC_DEFAULT;
    currentCursor = cursor;
    (void)currentCursor;
}

MouseCursor screenGetCurrentMouseCursor() {
    return storedMouseCursor;
}

void screenShowMouseCursor(bool visible) {
    (void)visible;
}

int screenGetCharWidth() {
    return CHAR_W * displayScale;
}

/*
 * Track mouse cursor position in character cells.
 * The actual rendering is done in screenSwapBuffers().
 */
void screenDrawMouseCursor(int charX, int charY, MouseCursor cursor) {
    mouseCursorX = charX;
    mouseCursorY = charY;
    storedMouseCursor = cursor;
}

/**
 * Return the SDL window (for event_sdl2.cpp to access).
 */
SDL_Window* screenGetWindow() {
    return sdlWindow;
}
