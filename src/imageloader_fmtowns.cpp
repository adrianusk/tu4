/*
 * imageloader_fmtowns.cpp
 */

#include "error.h"
#include "image.h"

/**
 * Loads in an FM TOWNS files, which we assume is 16 bits.
 * Only TIF format is implemented, PIC is not handled.
 */
Image *loadImage_fmTowns(U4FILE *file, int width, int height, int bpp) {
    const int offset = 510;     // 510 for TIF.

    if (width == -1 || height == -1 || bpp == -1) {
          errorFatal("dimensions not set for fmtowns image");
    }

    ASSERT((bpp == 16) | (bpp == 4), "invalid bpp: %d", bpp);

    long rawLen = file->length() - offset;
    file->seek(offset,0);
    unsigned char *raw = (unsigned char *) malloc(rawLen);
    file->read(raw, 1, rawLen);

    long requiredLength = (width * height * bpp / 8);
    if (rawLen < requiredLength) {
        if (raw)
            free(raw);
        errorWarning("FMTOWNS Image of size %ld does not fit anticipated size %ld", rawLen, requiredLength);
        return NULL;
    }

    Image *image = Image::create(width, height);
    if (!image) {
        if (raw)
            free(raw);
        return NULL;
    }

    if (bpp == 4)
    {
        setFromRawData(image, width, height, bpp, raw, stdPalette(bpp));
//      if (width % 2)
//          errorFatal("FMTOWNS 4bit images cannot handle widths not divisible by 2!");
//      unsigned char nibble_mask = 0x0F;
//        for (int y = 0; y < height; y++)
//        {
//            for (int x = 0; x < width; x+=2)
//            {
//              int byte = raw[(y * width + x) / 2];
//              image->putPixelIndex(x  ,y,(byte & nibble_mask)  << 4);
//              image->putPixelIndex(x+1,y,(byte              )      );
//            }
//        }
    }
    else if (bpp == 16)
    {
    // FM-Towns 16-bit pixel format (little-endian):
    //   Bit 15: transparency (0=opaque, 1=transparent)
    //   Bits 10-14: Green
    //   Bits 5-9: Red
    //   Bits 0-4: Blue

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            unsigned char byte0 = raw[(y * width + x) * 2];
            unsigned char byte1 = raw[(y * width + x) * 2 + 1];

            int pixel = byte0 | (byte1 << 8);

            int b = (pixel & 0x1F) << 3;
            int r = ((pixel >> 5) & 0x1F) << 3;
            int g = ((pixel >> 10) & 0x1F) << 3;
            int a = (pixel & 0x8000) ? IM_TRANSPARENT : IM_OPAQUE;

            image->putPixel(x, y, b, g, r, a);
        }
    }
    }

    free(raw);
    return image;
}
