// ega_palette.cpp — EGA palette, L*a*b* colour-diff table, and palettize.
//
// Palette/palettize logic ported from Playscii palette.py / image_convert.py
//   © 2014-2021 JP LeBreton, MIT License. See THIRD-PARTY-LICENSES.
//
// Determinism notes (verified against PIL, 2026-09-18):
//   * The 17-entry palette (idx0 transparent + 16 canonical EGA) is exactly
//     what Playscii scans from palettes/ega.png.
//   * Source images are U4-EGA-decoded (u4dec uses a hardcoded EGA table in
//     src/util/pngconv.c: 0/80/82/162/168/170/255-ish). Each u4dec colour maps
//     1:1 and UNAMBIGUOUSLY to a canonical EGA index (deltas 2..8, no ties), so
//     nearest-EGA (Euclidean) reproduces PIL quantize() for every u4dec colour.
//   * EGA black (0,0,0) -> palette index 0 (transparent slot), matching PIL.

#include "ega_palette.h"
#include "lab_color.h"

namespace tu4setup {

// Canonical EGA 0..15 (Playscii palettes/ega.png order).
static const uint8_t kEga[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},
    {170,170,170},{85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},
    {255,85,255},{255,255,85},{255,255,255}
};

EgaPalette::EgaPalette() {
    rgb[0][0]=rgb[0][1]=rgb[0][2]=0;                 // slot 0 = transparent
    for (int i = 0; i < 16; ++i) {
        rgb[i+1][0]=kEga[i][0]; rgb[i+1][1]=kEga[i][1]; rgb[i+1][2]=kEga[i][2];
    }
    for (int i = 0; i < NCOLORS; ++i)
        for (int j = 0; j < NCOLORS; ++j) {
            double l1,a1,b1,l2,a2,b2;
            rgb_to_lab(rgb[i][0],rgb[i][1],rgb[i][2],l1,a1,b1);
            rgb_to_lab(rgb[j][0],rgb[j][1],rgb[j][2],l2,a2,b2);
            diff[i][j] = (float)lab_color_diff(l1,a1,b1,l2,a2,b2);  // float32 (np.float32)
        }
}

int EgaPalette::nearestIndex(int r, int g, int b) const {
    if (r == 0 && g == 0 && b == 0) return 0;   // transparent slot wins for black
    int best = 1; long bestd = 1L<<62;
    for (int i = 0; i < 16; ++i) {
        long dr = r - kEga[i][0], dg = g - kEga[i][1], db = b - kEga[i][2];
        long d = dr*dr + dg*dg + db*db;
        if (d < bestd) { bestd = d; best = i + 1; }
    }
    return best;
}

} // namespace tu4setup
