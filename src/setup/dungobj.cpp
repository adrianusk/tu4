// dungobj.cpp — DUNGOBJ0/DUNGOBJ1 helpers (see dungobj.h).

#include "dungobj.h"
#include "decode.h"     // reuse the u4dec EGA table via egaExpand-style expand
#include <cstdio>
#include <vector>

namespace tu4setup {

// SHAPES uses the SAME u4dec EGA palette as the screen assets. We reuse
// egaExpand() by feeding it a single-tile-wide (16 px) sub-blob per tile.
// SHAPES.EGA layout: tile T occupies bytes [T*128 .. T*128+128), each tile is
// 16x16 px, 4bpp, 2 px/byte, high nibble = left pixel (same as egaExpand 4bpp).
RgbImage shapesStackVertical(const std::vector<uint8_t> &shapesRaw,
                             const std::vector<int> &ids) {
    const int TILE_PX = 16;
    const int TILE_BYTES = TILE_PX * TILE_PX / 2;   // 128
    int n = (int)ids.size();
    if (n == 0) return RgbImage();

    RgbImage out(TILE_PX, TILE_PX * n);             // 16 x (16*n), stacked
    for (int t = 0; t < n; ++t) {
        int id = ids[t];
        size_t base = (size_t)id * TILE_BYTES;
        if (base + TILE_BYTES > shapesRaw.size()) {
            std::fprintf(stderr, "dungobj: SHAPES id %d out of range\n", id);
            return RgbImage();
        }
        // Expand this one 16x16 tile via egaExpand (bits=4 for 128 bytes/256 px).
        std::vector<uint8_t> tileBytes(shapesRaw.begin() + base,
                                       shapesRaw.begin() + base + TILE_BYTES);
        RgbImage tile = egaExpand(tileBytes, TILE_PX, TILE_PX);
        if (tile.w == 0) { std::fprintf(stderr, "dungobj: expand id %d failed\n", id); return RgbImage(); }
        // Blit into the stack at row t*16.
        for (int y = 0; y < TILE_PX; ++y)
            for (int x = 0; x < TILE_PX; ++x) {
                int r, g, bb; tile.get(x, y, r, g, bb);
                out.set(x, t * TILE_PX + y, r, g, bb);
            }
    }
    return out;
}

// A cell is full-black iff char 0x00 & attr 0x00 (Playscii's RGB-flatten form).
static inline bool isBlackCell(uint8_t ch, uint8_t attr) {
    return ch == 0x00 && attr == 0x00;
}

void allBlackToTransparent(std::vector<uint8_t> &payload) {
    for (size_t i = 0; i + 1 < payload.size(); i += 2) {
        if (isBlackCell(payload[i], payload[i + 1]))
            payload[i] = 0x20;              // -> transparent space (attr kept 0x00)
    }
}

// Cell (r,c) within a single tile: byte index into `payload` for tile `t`
// whose top row is `t*tileChars`. Layout: `tileChars` cols, 2 bytes/cell,
// rows contiguous across the whole payload.
static inline size_t cellIndex(int tileTopRow, int r, int c, int tileChars) {
    int row = tileTopRow + r;
    return (size_t)(row * tileChars + c) * 2;
}

// Count 8-connected neighbours of (r,c) that are NON-black, per a precomputed
// black map `blk` for THIS tile (row-major, tileChars x tileChars). Off-tile
// neighbours (outside [0,tileChars)) count as BLACK per the project owner's
// rule (option A), so they are simply not counted. Using a snapshot (not the
// live payload) ensures the rule is evaluated against the ORIGINAL black map,
// so a cell turned transparent earlier in the pass cannot be miscounted as a
// non-black neighbour of a later cell.
static int nonBlackNeighbours8(const std::vector<uint8_t> &blk,
                               int r, int c, int tileChars) {
    int count = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr, nc = c + dc;
            if (nr < 0 || nr >= tileChars || nc < 0 || nc >= tileChars)
                continue;                    // off-tile == black, don't count
            if (!blk[nr * tileChars + nc])   // 0 => non-black
                ++count;
        }
    }
    return count;
}

void objBlackToTransparent(std::vector<uint8_t> &payload,
                           int tileChars, int numTiles, bool isObj0) {
    for (int t = 0; t < numTiles; ++t) {
        int top = t * tileChars;             // this tile's top row
        // Object identity is the tile index (0=fountain,1=chest,2=orb,3=altar).
        // Snapshot the ORIGINAL black map for this tile so the chest's
        // neighbour test is evaluated before any cell is mutated to 0x20.
        std::vector<uint8_t> blk((size_t)tileChars * tileChars, 0);
        for (int r = 0; r < tileChars; ++r)
            for (int c = 0; c < tileChars; ++c) {
                size_t i = cellIndex(top, r, c, tileChars);
                blk[r * tileChars + c] =
                    isBlackCell(payload[i], payload[i + 1]) ? 1 : 0;
            }

        for (int r = 0; r < tileChars; ++r) {
            for (int c = 0; c < tileChars; ++c) {
                if (!blk[r * tileChars + c])
                    continue;                // only black cells are ever changed
                size_t i = cellIndex(top, r, c, tileChars);
                switch (t) {
                case 0:                      // fountain: all black -> opaque
                    break;                   //   (leave unchanged)
                case 1:                      // chest
                    if (isObj0) {            // DUNGOBJ0: >=1 non-black nbr -> opaque
                        int n = nonBlackNeighbours8(blk, r, c, tileChars);
                        if (n < 1)
                            payload[i] = 0x20;   // else transparent
                    } else {                 // DUNGOBJ1: all black -> transparent
                        payload[i] = 0x20;
                    }
                    break;
                case 2:                      // orb: all black -> transparent
                case 3:                      // altar: all black -> transparent
                default:
                    payload[i] = 0x20;
                    break;
                }
            }
        }
    }
}

} // namespace tu4setup
