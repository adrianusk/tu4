/*
 * imageloader.h
 */

#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include "image.h"
#include "u4file.h"

enum ImageFiletype {
    FTYPE_UNKNOWN,
    FTYPE_PNG,
    FTYPE_U4RAW,
    FTYPE_U4RLE,
    FTYPE_U4LZW,
    FTYPE_U5LZW,
    FTYPE_FMTOWNS,
    FTYPE_FMTOWNS_PIC,
    FTYPE_FMTOWNS_TIF,
    FTYPE_ATLAS,        // Special internal type for ImageInfo.
    FTYPE_ASP_TILES,    // Text-mode tiles (serialized 4x4 char+attr, BSAVE format)
    FTYPE_ASP_SCREEN,   // Text-mode full screen (80x50 char+attr, BSAVE format)
    FTYPE_ASP_CHARSET,  // Text-mode charset (256 char+attr pairs, BSAVE format)
    FTYPE_ASP_MOONPHASES, // Text-mode moon phases (8 tiles of 2x2 char+attr, BSAVE format)
    FTYPE_ASP_DUNGEON   // Text-mode dungeon image (44x44 char+attr, BSAVE format)
};

#define BPP_CLUT8   -8

Image* loadImage(U4FILE *file, int ftype, int width, int height, int bpp);

#endif /* IMAGELOADER_H */
