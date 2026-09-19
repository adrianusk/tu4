// png2asp.cpp — image -> char/fg/bg matcher + BSAVE ASP encoder.
//
// Matcher ported 1:1 from Playscii image_convert.py:
//   get_color_combos_for_block / get_best_tile_for_block / update()
//   © 2014-2021 JP LeBreton, MIT License. See THIRD-PARTY-LICENSES.
//
// Faithfulness requirements for byte-exact output vs Playscii:
//   * unique colours come sorted ASCENDING (np.unique), then STABLE-sorted by
//     descending count -> equal-count colours keep ascending-index order.
//   * combos: nested loop over count-sorted colours, (c1,c2) with c1!=c2,
//     skip if (c1,c2) already present. Both orders allowed. Combo (c1,c2) is
//     unpacked as (bg,fg).
//   * per glyph 0..255 (charset order == CP437), render 1-bit block recoloured
//     to (fg where glyph pixel set, bg elsewhere); sum L*a*b* colour-diff table
//     over the 64 pixels; strict `<` keeps first minimum; diff==0 short-circuits.
//   * update(): fg==0 -> darkest_index, bg==0 -> darkest_index.

#include "png2asp.h"
#include <algorithm>
#include <cstdio>

namespace tu4setup {

// Returns unique palette indices in the block, sorted ascending, with counts.
static void uniqueColors(const IndexImage &img, int bx, int by,
                         std::vector<int> &colors, std::vector<int> &counts) {
    int cnt[17] = {0};
    for (int yy = 0; yy < 8; ++yy)
        for (int xx = 0; xx < 8; ++xx)
            cnt[img.at(bx*8+xx, by*8+yy)]++;
    for (int i = 0; i < 17; ++i)          // ascending index order (np.unique)
        if (cnt[i]) { colors.push_back(i); counts.push_back(cnt[i]); }
}

Cell matchBlock(const IndexImage &img, const Font8x8 &font,
                const EgaPalette &pal, int bx, int by) {
    std::vector<int> colors, counts;
    uniqueColors(img, bx, by, colors, counts);

    // Single-colour block -> (char 0, fg 0, bg = that colour) [or 0 if none].
    if (colors.size() <= 1) {
        int bg = colors.empty() ? 0 : colors[0];
        Cell c{0, 0, (uint8_t)bg};
        // update() post-step
        if (c.fg == 0) c.fg = EgaPalette::DARKEST;
        if (c.bg == 0) c.bg = EgaPalette::DARKEST;
        return c;
    }

    // Order colours by descending count, STABLE (preserve ascending index on ties).
    std::vector<int> order(colors.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b){ return counts[a] > counts[b]; });

    // Build combos: (c1,c2) c1!=c2, skip duplicates. (c1,c2) == (bg,fg).
    std::vector<std::pair<int,int>> combos;
    for (int oi : order) for (int oj : order) {
        int c1 = colors[oi], c2 = colors[oj];
        if (c1 == c2) continue;
        bool dup = false;
        for (auto &p : combos) if (p.first==c1 && p.second==c2) { dup=true; break; }
        if (dup) continue;
        combos.emplace_back(c1, c2);
    }

    int best_char = 0; float best_diff = 9.999999e12f;
    int best_fg = 0, best_bg = 0;
    // Read the source block once.
    uint8_t src[8][8];
    for (int yy=0; yy<8; ++yy) for (int xx=0; xx<8; ++xx) src[yy][xx]=img.at(bx*8+xx,by*8+yy);

    for (auto &combo : combos) {
        int bg = combo.first, fg = combo.second;
        for (int glyph = 0; glyph < 256; ++glyph) {
            // Build the 64 per-pixel diffs in ROW-MAJOR order, then sum them
            // with numpy's EXACT algorithm (np.sum over the flattened 8x8
            // block). numpy uses pairwise summation; for n=64 (<=128) that is
            // an 8-accumulator unrolled loop combined as
            //   ((r0+r1)+(r2+r3))+((r4+r5)+(r6+r7)).
            // Reproducing this bit-for-bit is REQUIRED: genuine glyph ties
            // differ only in the last FP bit, and the summation order decides
            // the winner. A naive left-to-right sum flips ~1.7% of cells.
            // Per-pixel diffs are float32 (pal.diff is float32, matching
            // Playscii's np.float32 color_diffs). numpy sums the flattened
            // float32 8x8 block with pairwise summation in float32 accumulators;
            // reproduce that EXACTLY (float, not double) or genuine ties resolve
            // to different glyphs.
            float a[64];
            int n = 0;
            for (int yy=0; yy<8; ++yy)
                for (int xx=0; xx<8; ++xx) {
                    int cc = font.pixel(glyph, xx, yy) ? fg : bg;  // 1-bit recolour
                    a[n++] = pal.diff[src[yy][xx]][cc];
                }
            float r0=a[0],r1=a[1],r2=a[2],r3=a[3],r4=a[4],r5=a[5],r6=a[6],r7=a[7];
            for (int i=8; i+8<=64; i+=8) {
                r0+=a[i]; r1+=a[i+1]; r2+=a[i+2]; r3+=a[i+3];
                r4+=a[i+4]; r5+=a[i+5]; r6+=a[i+6]; r7+=a[i+7];
            }
            float diff = ((r0+r1)+(r2+r3))+((r4+r5)+(r6+r7));
            if (diff == 0.0) {                 // exact -> return immediately
                Cell c{(uint8_t)glyph,(uint8_t)fg,(uint8_t)bg};
                if (c.fg==0) c.fg=EgaPalette::DARKEST;
                if (c.bg==0) c.bg=EgaPalette::DARKEST;
                return c;
            }
            if (diff < best_diff) { best_diff=diff; best_char=glyph; best_fg=fg; best_bg=bg; }
        }
    }
    Cell c{(uint8_t)best_char,(uint8_t)best_fg,(uint8_t)best_bg};
    if (c.fg==0) c.fg=EgaPalette::DARKEST;
    if (c.bg==0) c.bg=EgaPalette::DARKEST;
    return c;
}

std::vector<uint8_t> imageToPayload(const IndexImage &img, const Font8x8 &font,
                                    const EgaPalette &pal, int W, int H) {
    std::vector<uint8_t> payload((size_t)W*H*2, 0);
    for (int cy = 0; cy < H; ++cy) {
        for (int cx = 0; cx < W; ++cx) {
            Cell c = matchBlock(img, font, pal, cx, cy);
            int fg_ega = c.fg - 1; if (fg_ega < 0) fg_ega = 0;
            int bg_ega = c.bg - 1; if (bg_ega < 0) bg_ega = 0;
            size_t i = (size_t)(cy*W + cx) * 2;
            payload[i]   = c.ch;
            payload[i+1] = (uint8_t)(((bg_ega & 0xF) << 4) | (fg_ega & 0xF));
        }
    }
    return payload;
}

bool writeAsp(const char *path, const std::vector<uint8_t> &payload) {
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    uint32_t len = (uint32_t)payload.size();
    uint8_t hdr[7] = { 0xFD, 0x00, 0xB8, 0x40, 0x20,
                       (uint8_t)(len & 0xFF), (uint8_t)((len>>8) & 0xFF) };
    std::fwrite(hdr, 1, 7, f);
    std::fwrite(payload.data(), 1, payload.size(), f);
    std::fclose(f);
    return true;
}

} // namespace tu4setup
