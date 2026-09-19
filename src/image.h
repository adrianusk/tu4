/*
 * image.h - Text-mode image (ASP data container)
 *
 * In text mode, Image holds raw char+attr byte data from ASP files.
 * No pixel operations. The class name is kept so existing code that
 * passes Image* pointers still compiles.
 */

#ifndef IMAGE_H
#define IMAGE_H

#include <cstdint>
#include <cstddef>

#define IM_OPAQUE       255
#define IM_TRANSPARENT  0

struct RGBA {
    uint8_t r, g, b, a;
};

/**
 * Image class repurposed as a container for ASP text-mode data.
 * Holds raw char+attr byte pairs loaded from ASP files.
 */
class Image {
public:
    /**
     * Create an Image to hold ASP data of the given size.
     * Used by imageloader to allocate space before reading file.
     */
    static Image* createFromAsp(int dataSize);

    /**
     * Create a dummy/empty Image (for compatibility with code that
     * calls Image::create).
     */
    static Image* create(int w, int h);
    static int enableBlend(int on) { (void)on; return 0; }
    static RGBA black;

    ~Image();

    /** Get pointer to the raw ASP data (char+attr byte pairs). */
    uint8_t* getAspData() { return aspData; }
    const uint8_t* getAspData() const { return aspData; }

    /** Get the size of the ASP data in bytes. */
    int getDataSize() const { return dataSize; }

    /* Compatibility accessors (return 0 for unused pixel dimensions) */
    int width() const { return w; }
    int height() const { return h; }

    /** Column stride for drawSubRect (default 80 for screen ASP). */
    int getCols() const { return cols; }
    void setCols(int c) { cols = c; }

    /** Enable transparency: space (0x20) + attr 0x00 cells are not drawn. */
    bool isTransparent() const { return transparent; }
    void setTransparent(bool t) { transparent = t; }

    /* Stub methods for code that references Image pixel operations.
     * These are no-ops in text mode. */
    void setPalette(const RGBA* palette, int ncolors) { (void)palette; (void)ncolors; }
    void fillRect(int x, int y, int w, int h, int r, int g, int b, int a = IM_OPAQUE) {
        (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; (void)a;
    }
    /* Draw methods — implemented in image.cpp */
    void draw(int x, int y) const;
    void draw(Image* dest, int x, int y) const;
    void drawSubRect(int x, int y, int rx, int ry, int rw, int rh) const;
    void drawHighlighted() {}

private:
    Image();

    uint8_t* aspData;   /* raw char+attr data */
    int dataSize;       /* size of aspData in bytes */
    int w, h;           /* logical dimensions (chars for screen, tiles for tileset) */
    int cols;           /* column stride for draw/drawSubRect */
    bool transparent;   /* if true, skip space+0x00 cells during draw */

    // disallow assignments, copy construction
    Image(const Image&);
    const Image& operator=(const Image&);
};

#endif /* IMAGE_H */
