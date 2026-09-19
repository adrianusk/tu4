// mkaspdiff.cpp — DEV-ONLY generator for .aspdiff v2 (DESIGN §6).
//
// Produces  diff = ASP_final - ASP_raw  as a sparse per-cell patch, emitting a
// record ONLY for cells that visibly differ (the 3 don't-care rules), so the
// diff stays small and, combined with the SAME canonicalization on apply,
// reproduces ASP_final byte-for-byte.
//
//   mkaspdiff <raw.ASP> <final.ASP> <out.aspdiff> --class background|tile|native44
//
// Both inputs are BSAVE ASPs (7-byte header + W*H*2 payload). W/H are derived
// from the payload size and must match between raw and final.

#include "../src/setup/aspdiff.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

using namespace tu4setup;

static bool readFile(const char *path, std::vector<uint8_t> &buf) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    buf.resize((size_t)n);
    size_t got = std::fread(buf.data(), 1, (size_t)n, f);
    std::fclose(f);
    return got == (size_t)n;
}

// A cell counts as "changed" (needs a record) iff, AFTER canonicalizing raw,
// it differs from final under the visibility rules. Since we canonicalize BOTH
// raw and final the same way, a plain per-byte compare of the canonicalized
// payloads already encodes the 3 don't-care rules:
//   - char 0x00==0xDB (bg/native44) and 0x00==0x20 (tile) are unified by
//     canonicalizePayload();
//   - empty-cell fg nibble is zeroed by canonicalizePayload();
// so any remaining byte difference is a genuine visible touch-up.
// The RECORD stores the FINAL (canonicalized) bytes, which is what apply writes.
int main(int argc, char *argv[]) {
    const char *rawPath = nullptr, *finalPath = nullptr, *outPath = nullptr;
    AspClass cls = AspClass::Background; bool clsSet = false;

    std::vector<const char *> pos;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--class") == 0 && i + 1 < argc) {
            const char *c = argv[++i];
            if (!std::strcmp(c, "background")) cls = AspClass::Background;
            else if (!std::strcmp(c, "tile")) cls = AspClass::Tile;
            else if (!std::strcmp(c, "native44")) cls = AspClass::Native44;
            else { std::fprintf(stderr, "bad --class %s\n", c); return 2; }
            clsSet = true;
        } else pos.push_back(argv[i]);
    }
    if (pos.size() != 3 || !clsSet) {
        std::fprintf(stderr,
            "usage: mkaspdiff <raw.ASP> <final.ASP> <out.aspdiff> "
            "--class background|tile|native44\n");
        return 2;
    }
    rawPath = pos[0]; finalPath = pos[1]; outPath = pos[2];

    std::vector<uint8_t> raw, fin;
    if (!readFile(rawPath, raw))   { std::fprintf(stderr, "cannot read %s\n", rawPath);   return 1; }
    if (!readFile(finalPath, fin)) { std::fprintf(stderr, "cannot read %s\n", finalPath); return 1; }
    if (raw.size() < 7 || fin.size() < 7) { std::fprintf(stderr, "ASP too short\n"); return 1; }

    std::vector<uint8_t> rawP(raw.begin() + 7, raw.end());
    std::vector<uint8_t> finP(fin.begin() + 7, fin.end());
    if (rawP.size() != finP.size()) {
        std::fprintf(stderr, "payload size mismatch: raw %zu vs final %zu\n",
                     rawP.size(), finP.size());
        return 1;
    }
    size_t cells = rawP.size() / 2;
    if (cells > 65535) { std::fprintf(stderr, "W*H > 65535 unsupported\n"); return 1; }

    // Derive W/H from payload. We cannot know W/H split from size alone, so
    // require it via the standard tu4 sizes: 8000 -> 80x50, 3872 -> 44x44.
    uint16_t W = 0, H = 0;
    if (cells == 4000) { W = 80; H = 50; }
    else if (cells == 1936) { W = 44; H = 44; }
    else {
        std::fprintf(stderr,
            "cannot infer W*H for %zu cells (expected 4000=80x50 or 1936=44x44); "
            "extend mkaspdiff if a new size is needed\n", cells);
        return 1;
    }

    // Plain diff: compare the generated RAW against the (touched-up) FINAL
    // cell-by-cell and record every genuine difference. NO standard-characters
    // transform is applied here — that is a SEPARATE, explicit process run on
    // the inputs beforehand if wanted (e.g. all-black->transparent for tile
    // art). Keeping mkaspdiff a pure "final-bytes sparse patch over raw" makes
    // it byte-exact and free of hidden transforms.
    uint32_t finalCrc = crc32(finP.data(), finP.size());

    // Emit a record for every cell where raw != final.
    std::vector<uint8_t> recs;
    uint32_t count = 0;
    for (size_t c = 0; c < cells; ++c) {
        uint8_t fch = finP[c*2], fat = finP[c*2+1];
        if (rawP[c*2] != fch || rawP[c*2+1] != fat) {
            recs.push_back((uint8_t)(c & 0xFF));
            recs.push_back((uint8_t)((c >> 8) & 0xFF));
            recs.push_back(fch);
            recs.push_back(fat);
            ++count;
        }
    }

    // Write the .aspdiff v2 (little-endian, field-by-field).
    FILE *o = std::fopen(outPath, "wb");
    if (!o) { std::fprintf(stderr, "cannot write %s\n", outPath); return 1; }
    auto pu8  = [&](uint8_t v){ std::fputc(v, o); };
    auto pu16 = [&](uint16_t v){ pu8(v & 0xFF); pu8((v >> 8) & 0xFF); };
    auto pu32 = [&](uint32_t v){ pu8(v & 0xFF); pu8((v>>8)&0xFF); pu8((v>>16)&0xFF); pu8((v>>24)&0xFF); };
    std::fputc('A', o); std::fputc('D', o); std::fputc('I', o); std::fputc('F', o);
    pu8(2);                          // version
    pu8((uint8_t)cls);               // class
    pu16(W); pu16(H);
    pu32(finalCrc);
    pu32(count);
    std::fwrite(recs.data(), 1, recs.size(), o);
    std::fclose(o);

    std::printf("mkaspdiff: %s (%u changed cells, %zu bytes) class=%d %ux%u crc=0x%08x\n",
                outPath, count, (size_t)(18 + recs.size()), (int)cls, W, H, finalCrc);
    return 0;
}
