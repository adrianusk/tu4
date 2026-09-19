// png2asp.h — image -> char/fg/bg matcher + BSAVE ASP encoder.
// Matcher ported from Playscii image_convert.py (get_color_combos_for_block /
// get_best_tile_for_block / update) — © 2014-2021 JP LeBreton, MIT License.
// See THIRD-PARTY-LICENSES.
#ifndef TU4SETUP_PNG2ASP_H
#define TU4SETUP_PNG2ASP_H
#include <cstdint>
#include <vector>
#include "ega_palette.h"

namespace tu4setup {

// A source image already scaled to (8*W x 8*H) and palettized to EGA indices
// (0..16). Row-major, size = (8*W)*(8*H).
struct IndexImage {
    int w, h;                 // pixel dimensions (multiples of 8)
    std::vector<uint8_t> px;  // palette indices 0..16
    uint8_t at(int x, int y) const { return px[y*w + x]; }
};

// The CP437 8x8 font as a 256-glyph 1-bit map: bit set (1) = fg pixel.
// Rows are MSB-first (bit7 = leftmost). font[glyph*8 + row].
struct Font8x8 {
    uint8_t rows[256*8];
    // pixel(glyph,x,y): 1 if fg pixel, else 0
    int pixel(int glyph, int x, int y) const {
        return (rows[glyph*8 + y] >> (7 - x)) & 1;
    }
};

// One output cell.
struct Cell { uint8_t ch; uint8_t fg; uint8_t bg; };  // fg/bg are palette idx 0..16

// Match one 8x8 block (top-left at bx*8,by*8) -> (char, fg, bg) palette indices.
// Faithful port of get_best_tile_for_block; applies update()'s fg/bg==0 ->
// darkest_index post-step.
Cell matchBlock(const IndexImage &img, const Font8x8 &font,
                const EgaPalette &pal, int bx, int by);

// Convert a whole IndexImage (W x H char cells) to a char+attr payload
// (W*H*2 bytes). attr = (bg_ega<<4)|fg_ega where ega = palidx-1.
std::vector<uint8_t> imageToPayload(const IndexImage &img, const Font8x8 &font,
                                    const EgaPalette &pal, int W, int H);

// Write a BSAVE .ASP: 7-byte header (FD, seg B800, off 2040, len) + payload.
bool writeAsp(const char *path, const std::vector<uint8_t> &payload);

} // namespace tu4setup
#endif
