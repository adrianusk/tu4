// decode.h — U4 data decode for tu4-setup: decompress dispatch + EGA->RGB
// expansion.  Collapses u4dec.c's decode + writePngFromEga's EGA-palette pixel
// expansion into one in-memory step (no PNG round-trip), so the resulting
// RgbImage holds EXACTLY the RGB pixels the authoring PNGs held.  This is the
// byte-exactness crux (DESIGN-tu4setup.md §8): the matcher/palettize path must
// see the same pixels PIL saw when it opened those PNGs.
#ifndef TU4SETUP_DECODE_H
#define TU4SETUP_DECODE_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include "image_ops.h"

namespace tu4setup {

enum class DecompAlg { Raw, Rle, Lzw };

// Decompress `in` per `alg`, matching u4dec.c's logic exactly.
//   raw -> copy; rle -> rleDecompress; lzw -> lzwDecompress.
// For rle, width/height are used only for u4dec.c's validity check
// (outlen*8 % (w*h)==0 and a power-of-two bpp); pass the intended image size.
// Returns the decompressed bytes; empty on failure.
std::vector<uint8_t> decompress(const std::vector<uint8_t> &in, DecompAlg alg,
                                int width, int height);

// Expand decoded EGA bytes into an RgbImage, reproducing pngconv.c
// writePngFromEga():
//   bits = (decoded.size()*8) / (width*height)   (1, 4, or 8 bpp)
//   4bpp: 2 pixels/byte, HIGH nibble = left pixel (libpng MSB-first).
//   1bpp: 8 pixels/byte, MSB = left pixel.
//   8bpp: 1 pixel/byte.
// Palette index -> RGB uses pngconv's setEgaPalette values (the u4dec EGA
// table, with its 162/168/170/82/80 deltas) for 1/4bpp; 8bpp is unsupported
// here (VGA uses an external u4vga.pal and is out of scope for EGA themes).
// Returns empty RgbImage on malformed input.
RgbImage egaExpand(const std::vector<uint8_t> &decoded, int width, int height);

} // namespace tu4setup
#endif
