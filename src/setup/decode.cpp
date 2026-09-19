// decode.cpp — U4 decompress dispatch + EGA->RGB expansion for tu4-setup.
//
// EGA palette values are copied VERBATIM from pngconv.c setEgaPalette() (the
// table u4dec.c bakes into the authoring PNGs).  They intentionally differ from
// the canonical EGA table in ega_palette.cpp (e.g. blue 0,0,162 vs 0,0,170);
// EgaPalette::nearestIndex() snaps each unambiguously back to a canonical index
// (deltas 2..8, no ties), reproducing PIL quantize on the authoring PNGs.

#include "decode.h"
#include <cstdio>
#include "../rle.h"
#include "../lzw/lzw.h"

namespace tu4setup {

// pngconv.c setEgaPalette(), indices 0..15 -> {r,g,b}.  Do NOT "correct" these
// to canonical EGA: byte-exactness depends on matching the authoring PNGs.
static const uint8_t kU4decEga[16][3] = {
    {0,   0,   0},    {0,   0,   162},  {0,   162, 0},    {0,   162, 162},
    {162, 0,   0},    {162, 0,   162},  {170, 85,  0},    {168, 168, 168},
    {82,  82,  82},   {80,  80,  255},  {80,  255, 80},   {80,  255, 255},
    {255, 80,  80},   {255, 80,  255},  {255, 255, 80},   {255, 255, 255}
};

std::vector<uint8_t> decompress(const std::vector<uint8_t> &in, DecompAlg alg,
                                int width, int height) {
    std::vector<uint8_t> out;
    if (in.empty()) return out;

    if (alg == DecompAlg::Raw) {
        out = in;                                  // u4dec.c: outdata = indata
        return out;
    }

    if (alg == DecompAlg::Lzw) {
        long outlen = lzwGetDecompressedSize((unsigned char *)in.data(),
                                             (long)in.size());
        if (outlen <= 0) return out;
        out.resize((size_t)outlen);
        lzwDecompress((unsigned char *)in.data(), out.data(), (long)in.size());
        return out;
    }

    // Rle: mirror u4dec.c's size + validity check.
    long outlen = rleGetDecompressedSize((unsigned char *)in.data(),
                                         (long)in.size());
    if (outlen <= 0) return out;
    if (width > 0 && height > 0) {
        long pixels = (long)width * height;
        bool cond1 = (outlen * 8) % pixels == 0;
        long bpp = cond1 ? (outlen * 8) / pixels : 0;
        bool cond2 = bpp > 0 && (bpp & (bpp - 1)) == 0;  // power of two
        if (!cond1 || !cond2) {
            std::fprintf(stderr,
                "tu4setup decompress(rle): invalid size for %dx%d "
                "(outlen=%ld)\n", width, height, outlen);
            return out;                            // empty -> caller errors
        }
    }
    out.resize((size_t)outlen);
    rleDecompress((unsigned char *)in.data(), (long)in.size(),
                  out.data(), outlen);
    return out;
}

RgbImage egaExpand(const std::vector<uint8_t> &decoded, int width, int height) {
    RgbImage img;
    if (width <= 0 || height <= 0 || decoded.empty()) return img;

    long pixels = (long)width * height;
    long bits = ((long)decoded.size() * 8) / pixels;   // 1, 4, or 8

    img = RgbImage(width, height);

    if (bits == 4) {
        // 4bpp packed: 2 pixels/byte, HIGH nibble = left pixel.
        // Row j-range in writePngFromEga is width*4/8 = width/2 bytes.
        size_t bytesPerRow = (size_t)width / 2;
        for (int y = 0; y < height; ++y) {
            size_t rowoff = (size_t)y * bytesPerRow;
            for (int x = 0; x < width; ++x) {
                size_t bi = rowoff + (size_t)(x >> 1);
                if (bi >= decoded.size()) return RgbImage();
                uint8_t byte = decoded[bi];
                int idx = (x & 1) ? (byte & 0x0F) : (byte >> 4);
                img.set(x, y, kU4decEga[idx][0], kU4decEga[idx][1],
                        kU4decEga[idx][2]);
            }
        }
        return img;
    }

    if (bits == 1) {
        // 1bpp: 8 pixels/byte, MSB = left pixel; palette entries 0 and 1
        // (setBWPalette in pngconv.c is black/white, but EGA 1bpp assets use
        // the EGA table indices 0/1 = black/blue). Match writePngFromEga:
        // palette_size==2 -> setBWPalette (0,0,0)/(255,255,255).
        size_t bytesPerRow = (size_t)(width + 7) / 8;
        for (int y = 0; y < height; ++y) {
            size_t rowoff = (size_t)y * bytesPerRow;
            for (int x = 0; x < width; ++x) {
                size_t bi = rowoff + (size_t)(x >> 3);
                if (bi >= decoded.size()) return RgbImage();
                int bit = (decoded[bi] >> (7 - (x & 7))) & 1;
                int v = bit ? 255 : 0;             // setBWPalette
                img.set(x, y, v, v, v);
            }
        }
        return img;
    }

    // 8bpp (VGA) is out of scope for EGA-theme regeneration.
    std::fprintf(stderr,
        "tu4setup egaExpand: unsupported bpp=%ld for %dx%d (only 1/4 bpp)\n",
        bits, width, height);
    return RgbImage();
}

} // namespace tu4setup
