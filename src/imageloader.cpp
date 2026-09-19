/*
 * imageloader.cpp - Text-mode image loader
 * Loads ASP files (BSAVE format: 7-byte header + char+attr data).
 * Pixel-based loaders (PNG, U4, FMTowns) are not used in text mode.
 */

#include <cstdio>
#include <cstring>
#include "debug.h"
#include "error.h"
#include "imageloader.h"
#include "imagemgr.h"
#include "screen.h"
#include "tu4.h"

#define ASP_HEADER_SIZE  7
/* ASP_SCREEN_SIZE (full-screen char+attr payload byte size) comes from screen.h. */
#define ASP_TILE_SIZE    32     /* 4 rows * 4 cols * 2 bytes per cell */

/**
 * Load an ASP screen file (80x50 char+attr pairs).
 * Skips the 7-byte BSAVE header, loads the body data.
 * Returns an Image holding the raw char+attr data.
 */
static Image* loadImage_aspScreen(U4FILE *file) {
    uint8_t header[ASP_HEADER_SIZE];

    if (u4fread(header, 1, ASP_HEADER_SIZE, file) != ASP_HEADER_SIZE)
        return NULL;

    /* Verify BSAVE marker */
    if (header[0] != 0xFD) {
        errorWarning("ASP file missing BSAVE marker (0xFD)");
        return NULL;
    }

    /* Read the data length from header (little-endian uint16 at offset 5) */
    int dataSize = header[5] | (header[6] << 8);
    /*
     * Native 44x44 map-area images (shrine runes, stone circle) carry a
     * 3872-byte payload and are drawn 1:1 into the map area. Only pad up to
     * the full 80x50 screen size (8000) for actual full-screen images.
     */
    if (dataSize != 44 * 44 * 2 && dataSize < ASP_SCREEN_SIZE)
        dataSize = ASP_SCREEN_SIZE;

    Image* img = Image::createFromAsp(dataSize);
    if (!img)
        return NULL;

    if (u4fread(img->getAspData(), 1, dataSize, file) != (size_t)dataSize) {
        delete img;
        return NULL;
    }

    return img;
}

/**
 * Load an ASP tiles file (256 tiles, 32 bytes each, serialized).
 * Skips the 7-byte BSAVE header, loads tile data.
 * Returns an Image holding the raw tile char+attr data.
 */
static Image* loadImage_aspTiles(U4FILE *file, int numTiles) {
    uint8_t header[ASP_HEADER_SIZE];

    if (u4fread(header, 1, ASP_HEADER_SIZE, file) != ASP_HEADER_SIZE)
        return NULL;

    /* Verify BSAVE marker */
    if (header[0] != 0xFD) {
        errorWarning("ASP tiles file missing BSAVE marker (0xFD)");
        return NULL;
    }

    int dataSize = numTiles * ASP_TILE_SIZE;

    Image* img = Image::createFromAsp(dataSize);
    if (!img)
        return NULL;

    if (u4fread(img->getAspData(), 1, dataSize, file) != (size_t)dataSize) {
        delete img;
        return NULL;
    }

    return img;
}

/**
 * Load a text-mode charset file (BSAVE format).
 * Contains 256 char+attr pairs (512 bytes) after the 7-byte header.
 */
static Image* loadImage_aspCharset(U4FILE *file, int tileCount) {
    uint8_t header[ASP_HEADER_SIZE];

    if (u4fread(header, 1, ASP_HEADER_SIZE, file) != ASP_HEADER_SIZE)
        return NULL;

    if (header[0] != 0xFD) {
        errorWarning("ASP charset file missing BSAVE marker (0xFD)");
        return NULL;
    }

    int dataSize = tileCount * 2;  /* char+attr pairs */

    Image* img = Image::createFromAsp(dataSize);
    if (!img)
        return NULL;

    if (u4fread(img->getAspData(), 1, dataSize, file) != (size_t)dataSize) {
        delete img;
        return NULL;
    }

    return img;
}

#define MOON_TILE_SIZE   8      /* 2 rows * 2 cols * 2 bytes per cell */
#define MOON_PHASE_COUNT 8

/**
 * Load a text-mode moon phase tile file (BSAVE format).
 * Contains 8 tiles of 2x2 char+attr pairs (8 bytes each = 64 bytes)
 * after the 7-byte header.
 */
static Image* loadImage_aspMoonphases(U4FILE *file) {
    uint8_t header[ASP_HEADER_SIZE];

    if (u4fread(header, 1, ASP_HEADER_SIZE, file) != ASP_HEADER_SIZE)
        return NULL;

    if (header[0] != 0xFD) {
        errorWarning("ASP moon phases file missing BSAVE marker (0xFD)");
        return NULL;
    }

    int dataSize = MOON_PHASE_COUNT * MOON_TILE_SIZE;

    Image* img = Image::createFromAsp(dataSize);
    if (!img)
        return NULL;

    if (u4fread(img->getAspData(), 1, dataSize, file) != (size_t)dataSize) {
        delete img;
        return NULL;
    }

    return img;
}

/**
 * Load a text-mode dungeon image file (BSAVE format).
 * Contains 44x44 char+attr pairs (3872 bytes) after the 7-byte header.
 * The 'width' parameter specifies columns (default 44).
 */
static Image* loadImage_aspDungeon(U4FILE *file, int cols) {
    uint8_t header[ASP_HEADER_SIZE];

    if (u4fread(header, 1, ASP_HEADER_SIZE, file) != ASP_HEADER_SIZE)
        return NULL;

    if (header[0] != 0xFD) {
        errorWarning("ASP dungeon file missing BSAVE marker (0xFD)");
        return NULL;
    }

    if (cols <= 0)
        cols = 44;
    int rows = cols;  /* square images */
    int dataSize = cols * rows * 2;

    Image* img = Image::createFromAsp(dataSize);
    if (!img)
        return NULL;

    img->setCols(cols);
    img->setTransparent(true);

    if (u4fread(img->getAspData(), 1, dataSize, file) != (size_t)dataSize) {
        delete img;
        return NULL;
    }

    return img;
}

/**
 * Main image loading dispatch.
 * For text mode, only ASP file types are supported.
 */
Image* loadImage(U4FILE *file, int ftype, int width, int height, int bpp) {
    switch (ftype) {
    case FTYPE_ASP_SCREEN:
        return loadImage_aspScreen(file);

    case FTYPE_ASP_TILES:
        /* 'width' field holds tile count (from 'tiles' attribute in XML) */
        return loadImage_aspTiles(file, width > 0 ? width : 256);

    case FTYPE_ASP_CHARSET:
        return loadImage_aspCharset(file, width > 0 ? width : 256);

    case FTYPE_ASP_MOONPHASES:
        return loadImage_aspMoonphases(file);

    case FTYPE_ASP_DUNGEON:
        /* 'width' field holds column count (from 'width' attribute in XML) */
        return loadImage_aspDungeon(file, width > 0 ? width : 44);

    case FTYPE_PNG:
    case FTYPE_U4RAW:
    case FTYPE_U4RLE:
    case FTYPE_U4LZW:
    case FTYPE_U5LZW:
    case FTYPE_FMTOWNS:
    case FTYPE_FMTOWNS_PIC:
    case FTYPE_FMTOWNS_TIF:
        /* Pixel-based formats not supported in text mode */
        errorWarning("Pixel image format (%d) not supported in text mode", ftype);
        return NULL;

    default:
        errorWarning("Unknown image filetype: %d", ftype);
        return NULL;
    }
}
