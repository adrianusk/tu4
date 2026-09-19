// aspdiff.h — .aspdiff v2 load + apply (and canonicalization) for tu4-setup.
//
// A .aspdiff is a sparse per-cell patch encoding ASP_final - ASP_raw, capturing
// only the visible manual touch-ups on top of the matcher-regenerated raw
// (DESIGN-tu4setup.md §6). The apply path canonicalizes the regenerated raw the
// SAME way the generator did, so skipped (invisible-equal) cells already hold
// the final bytes; applying the records + a final CRC32 check yields a
// BYTE-EXACT ASP_final.
#ifndef TU4SETUP_ASPDIFF_H
#define TU4SETUP_ASPDIFF_H
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

namespace tu4setup {

// ASP class — selects the canonicalization rules (DESIGN §6).
enum class AspClass : uint8_t { Background = 0, Tile = 1, Native44 = 2 };

// Standard IEEE CRC-32 (poly 0xEDB88320, init/final 0xFFFFFFFF) — identical to
// zlib.crc32 / Python binascii.crc32, so it matches the stored final_crc32.
uint32_t crc32(const uint8_t *data, size_t len);

// Canonicalize an ASP PAYLOAD (the W*H*2 bytes AFTER the 7-byte header) in
// place, per `cls`. MUST be the same transform used at diff-generation time.
//   tile:               char 0x00 & attr 0x00 -> char 0x20 (transparent marker)
//   background/native44: char 0x00            -> char 0xDB (solid canonical)
//   ALL:                if canonical char is empty {0x00,0x20}, fg nibble -> 0
void canonicalizePayload(std::vector<uint8_t> &payload, AspClass cls);

// One parsed diff (v2). Records are stored as (index, char, attr) triples.
struct AspDiff {
    uint8_t  version = 0;
    AspClass cls = AspClass::Background;
    uint16_t width = 0, height = 0;
    uint32_t finalCrc32 = 0;
    struct Rec { uint16_t index; uint8_t ch; uint8_t attr; };
    std::vector<Rec> records;
};

// Load a .aspdiff v2 file (field-by-field, little-endian; NO struct cast).
// Returns false + fills `err` on malformed input / bad magic / wrong version.
bool loadAspDiff(const char *path, AspDiff &out, std::string &err);

// Apply `diff` to a freshly-regenerated raw payload (W*H*2 bytes, no header):
//   1. canonicalize `rawPayload` per diff.cls
//   2. for each record: payload[idx*2]=ch, payload[idx*2+1]=attr
//   3. verify crc32(payload) == diff.finalCrc32
// On success returns true and leaves the final payload in `rawPayload`.
// On CRC mismatch (pipeline drift / wrong U4 data) returns false + `err`.
bool applyAspDiff(const AspDiff &diff, std::vector<uint8_t> &rawPayload,
                  std::string &err);

} // namespace tu4setup
#endif
