// manifest.cpp — the EGA theme asset manifest (verified recipes).
// See .kiro/steering/tu4-setup-pipeline.md for the per-category recipes.

#include "manifest.h"

namespace tu4setup {

// crop for 44x44 codex/rune screens: 352x352 @ (16,16) on the 2x image.
// Helper macros keep the table readable.
#define SCREEN80(name, ega, alg, diff) \
    { name, ega, alg, 80, 50, 0, 0,0,0,0, TileMode::None, {}, diff }
#define SHRINE44(name, ega, diff) \
    { name, ega, DecompAlg::Rle, 44, 44, 2, 352,352,16,16, TileMode::None, {}, diff }

// Build the cumulative OR-overlay list for a codex virtue/principle: base is
// HONESTY, and each screen overlays all prior screens in codex order up to
// (but not including) itself. Returns the overlays for the screen at `idx`.
static std::vector<std::string> codexOverlays(int idx) {
    static const char *chain[] = {
        "HONESTY","COMPASSN","VALOR","JUSTICE","SACRIFIC","HONOR",
        "SPIRIT","HUMILITY","TRUTH","LOVE","COURAGE"
    };
    std::vector<std::string> ov;
    for (int j = 1; j <= idx; ++j)
        ov.push_back(std::string(chain[j]) + ".EGA:rle");
    return ov;
}

const std::vector<Recipe> &egaManifest() {
    static std::vector<Recipe> m;
    if (!m.empty()) return m;

    // --- 80x50 intro screens (category 1: no diff) ---
    m.push_back(SCREEN80("TREE",    "TREE.EGA",    DecompAlg::Lzw, false));
    m.push_back(SCREEN80("PORTAL",  "PORTAL.EGA",  DecompAlg::Lzw, false));
    m.push_back(SCREEN80("OUTSIDE", "OUTSIDE.EGA", DecompAlg::Lzw, false));
    m.push_back(SCREEN80("INSIDE",  "INSIDE.EGA",  DecompAlg::Lzw, false));
    m.push_back(SCREEN80("WAGON",   "WAGON.EGA",   DecompAlg::Lzw, false));
    m.push_back(SCREEN80("GYPSY",   "GYPSY.EGA",   DecompAlg::Lzw, false));

    // --- 80x50 screens with a shipped .aspdiff (category 2) ---
    m.push_back(SCREEN80("START",   "START.EGA",   DecompAlg::Rle, true));
    m.push_back(SCREEN80("ABACUS",  "ABACUS.EGA",  DecompAlg::Lzw, true));
    m.push_back(SCREEN80("ANIMATE", "ANIMATE.EGA", DecompAlg::Lzw, true));

    // --- TITLE: hybrid handled specially by the driver (upper rows + diff) ---
    m.push_back(SCREEN80("TITLE",   "TITLE.EGA",   DecompAlg::Lzw, true));

    // --- 44x44 shrine screens (category 2: touch-up diff) ---
    m.push_back(SHRINE44("RUNE_0", "RUNE_0.EGA", true));
    m.push_back(SHRINE44("RUNE_1", "RUNE_1.EGA", true));
    m.push_back(SHRINE44("RUNE_2", "RUNE_2.EGA", true));
    m.push_back(SHRINE44("RUNE_3", "RUNE_3.EGA", true));
    m.push_back(SHRINE44("RUNE_4", "RUNE_4.EGA", true));
    m.push_back(SHRINE44("RUNE_5", "RUNE_5.EGA", true));

    // --- 44x44 codex screens (category 1: gray conversion == baseline) ---
    m.push_back(SHRINE44("KEY7",     "KEY7.EGA",     false));
    m.push_back(SHRINE44("STONCRCL", "STONCRCL.EGA", false));
    // 8 virtues + 3 principles, cumulative OR-overlay (base HONESTY).
    static const char *chain[] = {
        "HONESTY","COMPASSN","VALOR","JUSTICE","SACRIFIC","HONOR",
        "SPIRIT","HUMILITY","TRUTH","LOVE","COURAGE"
    };
    for (int i = 0; i < 11; ++i) {
        Recipe r = SHRINE44(chain[i], "HONESTY.EGA", false);
        r.name = chain[i];
        // base EGA file is always HONESTY.EGA; overlays add the rest up to i.
        r.overlays = codexOverlays(i);
        m.push_back(r);
    }

    // --- SHAPES-derived tile art (category 1: per-object/all-black rule) ---
    m.push_back({ "DUNGOBJ0", "SHAPES.EGA", DecompAlg::Raw, 0,0, 0, 0,0,0,0, TileMode::DungObj0, {}, false });
    m.push_back({ "DUNGOBJ1", "SHAPES.EGA", DecompAlg::Raw, 0,0, 0, 0,0,0,0, TileMode::DungObj1, {}, false });
    m.push_back({ "DUNGNPC1", "SHAPES.EGA", DecompAlg::Raw, 0,0, 0, 0,0,0,0, TileMode::DungNpc1, {}, false });

    return m;
}

#undef SCREEN80
#undef SHRINE44

} // namespace tu4setup
