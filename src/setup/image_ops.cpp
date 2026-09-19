// image_ops.cpp — minimal RGB image ops for tu4-setup (no PIL dependency).
//
// The tu4-setup pipeline (DESIGN-tu4setup.md §1) resamples images in exactly
// ONE way: an integer NEAREST up-scale (the "scale2x -> 640x400" step, a clean
// 2x of the 320x200 decoded frame, used only for screens/runes/codex). All
// other transforms are pure crop and cumulative OR-overlay, which never
// resample. There is deliberately NO fractional resize (e.g. 320->44) anywhere
// in the pipeline: tiles are matched directly on 8x8 blocks of an
// already-final-size image.
//
// For an integer scale ratio, PIL's Image.resize(NEAREST) reduces to the
// center-sample formula src = floor((d + 0.5) * src / dst) (verified vs PIL
// 9.0.1 for integer ratios, 2026-09-18). We therefore implement only the
// integer-ratio case and hard-error on any non-integer ratio, so future
// pipeline drift is caught loudly instead of silently producing pixels that
// would not byte-match the Python authoring baseline (DESIGN §8).

#include "image_ops.h"
#include <cstdio>
#include <cstdlib>

namespace tu4setup {

RgbImage scaleNearest(const RgbImage &src, int dw, int dh) {
    if (src.w <= 0 || src.h <= 0 || dw <= 0 || dh <= 0) return RgbImage();

    // The pipeline only ever asks for an integer up-scale (typically 2x).
    // Enforce that: dw/dh must be exact integer multiples of the source.
    if (dw % src.w != 0 || dh % src.h != 0) {
        std::fprintf(stderr,
            "tu4setup scaleNearest: non-integer scale %dx%d -> %dx%d not "
            "supported (pipeline uses integer NEAREST only)\n",
            src.w, src.h, dw, dh);
        std::abort();
    }

    RgbImage dst(dw, dh);
    // src index for destination d: floor((d + 0.5) * src / dst). For an
    // integer ratio k this is exactly floor(d / k), i.e. block replication,
    // which is what PIL NEAREST produces for integer up-scales.
    for (int dy = 0; dy < dh; ++dy) {
        int sy = (int)(((double)dy + 0.5) * src.h / dh);
        if (sy >= src.h) sy = src.h - 1;
        for (int dx = 0; dx < dw; ++dx) {
            int sx = (int)(((double)dx + 0.5) * src.w / dw);
            if (sx >= src.w) sx = src.w - 1;
            int r, g, b;
            src.get(sx, sy, r, g, b);
            dst.set(dx, dy, r, g, b);
        }
    }
    return dst;
}

RgbImage crop(const RgbImage &src, int x0, int y0, int cw, int ch) {
    if (cw <= 0 || ch <= 0) return RgbImage();
    if (x0 < 0 || y0 < 0 || x0 + cw > src.w || y0 + ch > src.h) {
        std::fprintf(stderr,
            "tu4setup crop: region (%d,%d %dx%d) out of bounds for %dx%d\n",
            x0, y0, cw, ch, src.w, src.h);
        return RgbImage();
    }
    RgbImage dst(cw, ch);
    for (int y = 0; y < ch; ++y)
        for (int x = 0; x < cw; ++x) {
            int r, g, b;
            src.get(x0 + x, y0 + y, r, g, b);
            dst.set(x, y, r, g, b);
        }
    return dst;
}

void overlayNonBlack(RgbImage &acc, const RgbImage &top) {
    if (acc.w != top.w || acc.h != top.h) {
        std::fprintf(stderr,
            "tu4setup overlayNonBlack: size mismatch %dx%d vs %dx%d\n",
            acc.w, acc.h, top.w, top.h);
        return;
    }
    // Cumulative OR-overlay: any non-black (!= 0,0,0) pixel of `top` paints
    // over `acc`; index-0/black is treated as transparent.
    for (int y = 0; y < acc.h; ++y)
        for (int x = 0; x < acc.w; ++x) {
            int r, g, b;
            top.get(x, y, r, g, b);
            if (r != 0 || g != 0 || b != 0)
                acc.set(x, y, r, g, b);
        }
}

IndexImage palettize(const RgbImage &src, const EgaPalette &pal) {
    IndexImage out;
    out.w = src.w;
    out.h = src.h;
    out.px.resize((size_t)src.w * src.h);
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            int r, g, b;
            src.get(x, y, r, g, b);
            out.px[(size_t)y * src.w + x] = (uint8_t)pal.nearestIndex(r, g, b);
        }
    return out;
}

} // namespace tu4setup
