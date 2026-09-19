// image_ops.h — minimal RGB image ops for tu4-setup (no PIL dependency).
// nearest-scale reproduces PIL's Image.resize(NEAREST) exactly (center-based
// index: src = floor((d+0.5)*src/dst), clamped) — verified vs PIL 2026-09-18.
#ifndef TU4SETUP_IMAGE_OPS_H
#define TU4SETUP_IMAGE_OPS_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include "png2asp.h"
#include "ega_palette.h"

namespace tu4setup {

struct RgbImage {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGB, row-major, size = w*h*3
    RgbImage() {}
    RgbImage(int w_, int h_) : w(w_), h(h_), px((size_t)w_*h_*3, 0) {}
    inline void get(int x, int y, int &r, int &g, int &b) const {
        size_t i=((size_t)y*w+x)*3; r=px[i]; g=px[i+1]; b=px[i+2];
    }
    inline void set(int x, int y, int r, int g, int b) {
        size_t i=((size_t)y*w+x)*3; px[i]=(uint8_t)r; px[i+1]=(uint8_t)g; px[i+2]=(uint8_t)b;
    }
};

// Nearest-neighbour resize matching PIL Image.resize((dw,dh), NEAREST).
RgbImage scaleNearest(const RgbImage &src, int dw, int dh);

// Crop [x0,y0) .. [x0+cw, y0+ch). Out-of-bounds is an error (returns empty).
RgbImage crop(const RgbImage &src, int x0, int y0, int cw, int ch);

// Cumulative overlay: paint every non-black (!= 0,0,0) pixel of `top` over
// `acc` (index-0/black is transparent). Sizes must match.
void overlayNonBlack(RgbImage &acc, const RgbImage &top);

// Palettize an RGB image (already EGA-ish) to an IndexImage of EGA indices.
IndexImage palettize(const RgbImage &src, const EgaPalette &pal);

} // namespace tu4setup
#endif
