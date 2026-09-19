// ega_palette.h — EGA palette + palettize + L*a*b* colour-diff table.
// Palette-scan and palettize logic ported from Playscii palette.py / image_convert.py
//   © 2014-2021 JP LeBreton, MIT License. See THIRD-PARTY-LICENSES.
#ifndef TU4SETUP_EGA_PALETTE_H
#define TU4SETUP_EGA_PALETTE_H
#include <cstdint>
#include <vector>

namespace tu4setup {

// 17-entry palette matching Playscii's scan of palettes/ega.png:
//   index 0 = transparent (0,0,0), indices 1..16 = EGA 0..15 canonical.
// darkest_index = 1, lightest_index = 16 (per palette.py luminosity scan).
struct EgaPalette {
    static const int NCOLORS = 17;           // incl. transparent slot 0
    static const int DARKEST = 1;
    static const int LIGHTEST = 16;
    uint8_t rgb[NCOLORS][3];
    float   diff[NCOLORS][NCOLORS];          // L*a*b* colour-diff table (float32,
                                             // matching Playscii np.float32 color_diffs)

    EgaPalette();
    // Nearest palette index (0..16) for an RGB pixel, matching PIL quantize
    // to this palette (Euclidean in RGB, as PIL P-mode quantize uses).
    int nearestIndex(int r, int g, int b) const;
};

} // namespace tu4setup
#endif
