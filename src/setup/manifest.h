// manifest.h — per-asset recipe + the EGA theme manifest for tu4-setup.
//
// Each Recipe fully describes how to regenerate one asset from the user's U4
// data (see .kiro/steering/tu4-setup-pipeline.md). The batch driver iterates
// the manifest, regenerates each asset's raw payload, applies its .aspdiff if
// present, and writes the final ASP.
#ifndef TU4SETUP_MANIFEST_H
#define TU4SETUP_MANIFEST_H
#include <cstdint>
#include <vector>
#include <string>
#include "decode.h"

namespace tu4setup {

enum class TileMode { None, DungObj0, DungObj1, DungNpc1 };

struct Recipe {
    const char *name;        // asset name (output basename, no extension)
    const char *egaFile;     // source .EGA in the data dir (SHAPES.EGA for tiles)
    DecompAlg   alg;         // rle / lzw / raw
    int         cellsW, cellsH;  // ASP char grid (80x50 or 44x44); tiles: derived
    int         scale;       // 0 = auto aspect-fit, else integer factor
    int         cropW, cropH, cropX, cropY;  // 0 = no crop
    TileMode    tile;        // None for screens; else SHAPES-derived tile art
    // Cumulative OR-overlay sources (codex build-up), in order. "" terminated.
    std::vector<std::string> overlays;   // each "FILE.EGA[:alg]"
    bool        hasDiff;     // whether a <name>.aspdiff must be applied
};

// The EGA theme manifest (all regenerable assets).
const std::vector<Recipe> &egaManifest();

} // namespace tu4setup
#endif
