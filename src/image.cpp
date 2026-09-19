/*
 * image.cpp - Text-mode image (ASP data container)
 */

#include <cstring>
#include "image.h"
#include "screen.h"

Image::Image() : aspData(NULL), dataSize(0), w(0), h(0), cols(ASP_SCREEN_COLS), transparent(false) {
}

RGBA Image::black = {0, 0, 0, 255};

Image::~Image() {
    delete[] aspData;
}

/**
 * Create an Image to hold ASP data of the given byte size.
 * The caller (imageloader) fills the data after creation.
 */
Image* Image::createFromAsp(int size) {
    Image* img = new Image();
    img->aspData = new uint8_t[size];
    img->dataSize = size;
    /* For screen ASP: full-screen background (ASP_SCREEN_COLS x ASP_SCREEN_ROWS) */
    if (size == ASP_SCREEN_SIZE) {
        img->w = ASP_SCREEN_COLS;
        img->h = ASP_SCREEN_ROWS;
    }
    /* For native 44x44 map-area ASP (shrine runes, stone circle, etc.) */
    else if (size == 44 * 44 * 2) {
        img->w = 44;
        img->h = 44;
        img->cols = 44;
    }
    /* For tiles ASP: 256 tiles * 32 bytes = 8192 */
    else if (size == 8192) {
        img->w = 256;
        img->h = 32;
    }
    else {
        img->w = size;
        img->h = 1;
    }
    memset(img->aspData, 0, size);
    return img;
}

/**
 * Create a dummy Image (compatibility stub).
 * Some code paths may call Image::create(w,h) expecting a pixel buffer;
 * in text mode this returns a minimal empty image.
 */
Image* Image::create(int w, int h) {
    Image* img = new Image();
    img->w = w;
    img->h = h;
    img->dataSize = 0;
    img->aspData = NULL;
    return img;
}

/**
 * Draw the full ASP image to the text buffer at position (x, y).
 * For 80x50 screen ASP files, this blits the entire screen.
 * If transparent is set, cells with space (0x20) + attr 0x00 are skipped.
 */
void Image::draw(int x, int y) const {
    if (!aspData || dataSize < 2)
        return;

    /* Determine dimensions from cols field */
    int rows = dataSize / (cols * 2);
    if (rows > SCREEN_ROWS) rows = SCREEN_ROWS;

    for (int row = 0; row < rows; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= SCREEN_ROWS) continue;
        for (int col = 0; col < cols; col++) {
            int dx = x + col;
            if (dx < 0 || dx >= SCREEN_COLS) continue;
            int idx = (row * cols + col) * 2;
            uint8_t ch   = aspData[idx];
            uint8_t attr = aspData[idx + 1];
            if (transparent && ch == 0x20)
                continue;
            screenPutChar(dx, dy, ch, attr);
        }
    }
}

/**
 * Draw to another Image (no-op in text mode — used by dungeonview scratch buffer).
 */
void Image::draw(Image* dest, int x, int y) const {
    (void)dest; (void)x; (void)y;
}

/**
 * Draw a sub-rectangle of the ASP image to the text buffer.
 * (x, y) = destination screen position
 * (rx, ry, rw, rh) = source rectangle within the image (in character units)
 * If transparent is set, cells with space (0x20) + attr 0x00 are skipped.
 */
void Image::drawSubRect(int x, int y, int rx, int ry, int rw, int rh) const {
    if (!aspData || dataSize < 2)
        return;

    for (int row = 0; row < rh; row++) {
        int srcRow = ry + row;
        int dy = y + row;
        if (dy < 0 || dy >= SCREEN_ROWS) continue;
        for (int col = 0; col < rw; col++) {
            int srcCol = rx + col;
            int dx = x + col;
            if (dx < 0 || dx >= SCREEN_COLS) continue;
            int idx = (srcRow * cols + srcCol) * 2;
            if (idx + 1 >= dataSize) continue;
            uint8_t ch   = aspData[idx];
            uint8_t attr = aspData[idx + 1];
            if (transparent && ch == 0x20)
                continue;
            screenPutChar(dx, dy, ch, attr);
        }
    }
}
