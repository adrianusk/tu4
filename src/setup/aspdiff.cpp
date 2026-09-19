// aspdiff.cpp — .aspdiff v2 load + apply + canonicalization (DESIGN §6).

#include "aspdiff.h"
#include <cstdio>

namespace tu4setup {

// ---- CRC-32 (IEEE, matches zlib.crc32 / Python binascii.crc32) -------------
uint32_t crc32(const uint8_t *data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---- Canonicalization (must match the generator) ---------------------------
void canonicalizePayload(std::vector<uint8_t> &payload, AspClass cls) {
    // payload is char/attr pairs. Iterate per cell.
    for (size_t i = 0; i + 1 < payload.size(); i += 2) {
        uint8_t ch  = payload[i];
        uint8_t attr = payload[i + 1];

        if (cls == AspClass::Tile) {
            // transparent marker: char 0x00 & attr 0x00 -> char 0x20
            if (ch == 0x00 && attr == 0x00) { ch = 0x20; }
        } else {
            // background / native44: solid canonical: char 0x00 -> 0xDB
            if (ch == 0x00) { ch = 0xDB; }
        }

        // ALL: if the (canonical) char is empty {0x00,0x20}, fg nibble -> 0.
        // (0x00 only remains "empty" for tile class where attr!=0; for
        //  background/native44 a 0x00 became 0xDB above and is no longer empty.)
        if (ch == 0x00 || ch == 0x20) {
            attr = (uint8_t)(attr & 0xF0);   // clear low nibble (fg)
        }

        payload[i]     = ch;
        payload[i + 1] = attr;
    }
}

// ---- Load (field-by-field LE, no struct cast) ------------------------------
static bool rdFile(const char *path, std::vector<uint8_t> &buf) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    buf.resize((size_t)n);
    size_t got = std::fread(buf.data(), 1, (size_t)n, f);
    std::fclose(f);
    return got == (size_t)n;
}

bool loadAspDiff(const char *path, AspDiff &out, std::string &err) {
    std::vector<uint8_t> b;
    if (!rdFile(path, b)) { err = "cannot read aspdiff file"; return false; }
    // header = 4(magic)+1+1+2+2+4+4 = 18 bytes
    if (b.size() < 18) { err = "aspdiff too short"; return false; }
    size_t p = 0;
    auto u8  = [&]() -> uint8_t  { return b[p++]; };
    auto u16 = [&]() -> uint16_t { uint16_t v = (uint16_t)(b[p] | (b[p+1] << 8)); p += 2; return v; };
    auto u32 = [&]() -> uint32_t {
        uint32_t v = (uint32_t)b[p] | ((uint32_t)b[p+1] << 8) |
                     ((uint32_t)b[p+2] << 16) | ((uint32_t)b[p+3] << 24);
        p += 4; return v;
    };

    if (!(b[0]=='A' && b[1]=='D' && b[2]=='I' && b[3]=='F')) { err = "bad magic"; return false; }
    p = 4;
    out.version = u8();
    if (out.version != 2) { err = "unsupported aspdiff version"; return false; }
    uint8_t c = u8();
    if (c > 2) { err = "bad class"; return false; }
    out.cls = (AspClass)c;
    out.width  = u16();
    out.height = u16();
    out.finalCrc32 = u32();
    uint32_t count = u32();

    if (b.size() < 18 + (size_t)count * 4) { err = "aspdiff truncated records"; return false; }
    out.records.clear();
    out.records.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AspDiff::Rec r;
        r.index = u16();
        r.ch    = u8();
        r.attr  = u8();
        out.records.push_back(r);
    }
    return true;
}

// ---- Apply -----------------------------------------------------------------
bool applyAspDiff(const AspDiff &diff, std::vector<uint8_t> &rawPayload,
                  std::string &err) {
    size_t expect = (size_t)diff.width * diff.height * 2;
    if (rawPayload.size() != expect) {
        char buf[128];
        std::snprintf(buf, sizeof buf,
            "raw payload size %zu != expected %zu (%ux%u*2)",
            rawPayload.size(), expect, diff.width, diff.height);
        err = buf; return false;
    }

    // Apply records directly onto the generated raw. The diff was generated as
    // a plain raw-vs-final patch (mkaspdiff applies NO transform), so applying
    // each record reproduces the final bytes exactly. Any standard-characters
    // normalization (e.g. all-black->transparent for tile art) is a SEPARATE
    // step done to the raw BEFORE apply, if the diff was generated that way.
    for (const AspDiff::Rec &r : diff.records) {
        size_t off = (size_t)r.index * 2;
        if (off + 1 >= rawPayload.size()) { err = "record index out of range"; return false; }
        rawPayload[off]     = r.ch;
        rawPayload[off + 1] = r.attr;
    }

    // verify final CRC
    uint32_t got = crc32(rawPayload.data(), rawPayload.size());
    if (got != diff.finalCrc32) {
        char buf[160];
        std::snprintf(buf, sizeof buf,
            "asset regeneration mismatch (pipeline drift or wrong U4 data): "
            "final CRC32 0x%08x != expected 0x%08x; cannot produce ASP",
            got, diff.finalCrc32);
        err = buf; return false;
    }
    return true;
}

} // namespace tu4setup
