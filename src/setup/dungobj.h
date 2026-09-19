// dungobj.h — DUNGOBJ0/DUNGOBJ1 generation from SHAPES.EGA (see
// .kiro/steering/tu4-setup-pipeline.md "DUNGOBJ0/DUNGOBJ1 recipe").
//
// SHAPES.EGA = 256 tiles, 16x16 px, 4bpp raw (2 px/byte, high nibble = left).
// Object tiles by save ID: fountain=2, chest=60, orb=78, altar=74.
// Steps: crop object tiles -> stack VERTICALLY -> (caller) scale 8x NEAREST ->
// match to char tiles -> per-tile flood-fill transparency -> serialize.
#ifndef TU4SETUP_DUNGOBJ_H
#define TU4SETUP_DUNGOBJ_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include "image_ops.h"

namespace tu4setup {

// Decode the whole SHAPES.EGA blob into an RgbImage of `count` tiles stacked
// VERTICALLY (width 16, height 16*count), using the u4dec EGA palette (so it
// matches the authoring PNG, like egaExpand). `ids` are the SHAPES save IDs to
// crop, in stack order. Returns empty on malformed input.
RgbImage shapesStackVertical(const std::vector<uint8_t> &shapesRaw,
                             const std::vector<int> &ids);

// Turn every full-black cell (char 0x00 AND attr 0x00) into the transparent
// marker space 0x20 (attr kept 0x00), across the whole payload. Used for
// DUNGNPC1 (monster tiles have no opaque interior, so a blanket rule is
// correct).
void allBlackToTransparent(std::vector<uint8_t> &payload);

// Per-object transparency for the DUNGOBJ0/DUNGOBJ1 object sheet, replacing
// the old "all black -> space + shipped .aspdiff touch-up" scheme with a
// deterministic per-tile rule so NO diff is needed.
//
// The payload is `tileChars` columns wide and `tileChars*numTiles` rows tall,
// tiles stacked VERTICALLY: tile T occupies rows [T*tileChars,(T+1)*tileChars).
// Tile order (both DUNGOBJ0 and DUNGOBJ1): 0=fountain, 1=chest, 2=orb,
// 3=altar (DUNGOBJ0 only). A "black cell" is char 0x00 AND attr 0x00.
// "Transparent" = char 0x20 (attr kept 0x00); "opaque" = cell left unchanged.
// Rules (project owner, 2026-09-18/19):
//   fountain (0): all black -> opaque   (no change)
//   chest    (1): DUNGOBJ0 (isObj0=true): black -> opaque iff it has >= 1
//                 non-black 8-connected neighbour, else transparent (off-tile
//                 neighbours count as BLACK).
//                 DUNGOBJ1 (isObj0=false): all black -> transparent.
//   orb      (2): all black -> transparent
//   altar    (3): all black -> transparent
void objBlackToTransparent(std::vector<uint8_t> &payload,
                           int tileChars, int numTiles, bool isObj0);

} // namespace tu4setup
#endif
