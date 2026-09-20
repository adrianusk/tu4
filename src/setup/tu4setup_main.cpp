// tu4setup_main.cpp — tu4-setup orchestrator.
//
// Collapses the Python authoring path (u4dec -> PNG -> batch_png2psci ->
// psci2asp -> ASP) into ONE in-memory C++ pipeline, byte-identical to the
// Python matcher (DESIGN-tu4setup.md; verified 2026-09-19).
//
// Two modes:
//   * single asset (CLI args, back-compatible), and
//   * batch:  tu4-setup --all --data DIR --font PATH --diffs DIR --out DIR
//             regenerates every asset in the EGA manifest, applying each
//             asset's .aspdiff where present, writing final ASPs to --out.

#include "decode.h"
#include "image_ops.h"
#include "png2asp.h"
#include "ega_palette.h"
#include "dungobj.h"
#include "aspdiff.h"
#include "manifest.h"
#include "datasource.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <cmath>

using namespace tu4setup;

// ---- Read whole file -------------------------------------------------------
static bool readFile(const char *path, std::vector<uint8_t> &out) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n < 0) { std::fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = std::fread(out.data(), 1, (size_t)n, f);
    std::fclose(f);
    return got == (size_t)n;
}

// ---- Locate the Ultima IV data dir, mirroring tu4's search (u4file.cpp) -----
// (Ultima IV data location + reading is handled by DataSource, which supports
// both an unpacked dir and an ultima4.zip; see datasource.{h,cpp}.)

static bool loadFont(const char *path, Font8x8 &font) {
    std::vector<uint8_t> buf;
    if (!readFile(path, buf)) {
        std::fprintf(stderr, "tu4-setup: cannot read font %s\n", path);
        return false;
    }
    if (buf.size() != 256 * 8) {
        std::fprintf(stderr, "tu4-setup: font %s is %zu bytes (expected 2048)\n",
                     path, buf.size());
        return false;
    }
    std::memcpy(font.rows, buf.data(), 256 * 8);
    return true;
}

// Aspect-preserving fit (matches image_convert.py).
static void fitTarget(int srcW, int srcH, int cellsW, int cellsH,
                      int charW, int charH, int &outW, int &outH) {
    double artPixelW = (double)charW * cellsW;
    double artPixelH = (double)charH * cellsH;
    double ratio = std::min(artPixelH / srcH, artPixelW / srcW);
    outW = (int)std::floor((srcW * ratio) / charW) * charW;
    outH = (int)std::floor((srcH * ratio) / charH) * charH;
}

static DecompAlg parseAlg(const std::string &s, DecompAlg dflt) {
    if (s == "rle") return DecompAlg::Rle;
    if (s == "lzw") return DecompAlg::Lzw;
    if (s == "raw") return DecompAlg::Raw;
    return dflt;
}

// ---- Core: produce the RAW payload for one recipe (no diff applied) --------
// Returns false on error. On success `payload` holds the W*H*2 ASP payload
// (no 7-byte header). `outW`/`outH` receive the char grid (for header/reporting).
static bool generatePayload(const Recipe &r, const DataSource &data,
                            const Font8x8 &font, std::vector<uint8_t> &payload,
                            int &outW, int &outH) {
    const int srcW = 320, srcH = 200;
    EgaPalette pal;

    // ---- SHAPES-derived tile art (DUNGOBJ0/1, DUNGNPC1) ----
    if (r.tile != TileMode::None) {
        std::vector<uint8_t> shapes;
        if (!data.read(r.egaFile, shapes)) {
            std::fprintf(stderr, "tu4-setup: cannot read %s from %s\n", r.egaFile, data.location().c_str());
            return false;
        }
        std::vector<int> ids;
        int tileChars;
        if (r.tile == TileMode::DungNpc1) {
            const int base[] = { 144,148,152,156,160,164,168,176,180,184,188 };
            for (int c = 0; c < 11; ++c)
                for (int f = 0; f < 4; ++f) ids.push_back(base[c] + f);
            tileChars = 8;
        } else {
            ids = { 2, 60, 78 };
            if (r.tile == TileMode::DungObj0) ids.push_back(74);
            tileChars = (r.tile == TileMode::DungObj0) ? 16 : 8;
        }
        int numTiles = (int)ids.size();
        RgbImage stacked = shapesStackVertical(shapes, ids);
        if (stacked.w == 0) return false;
        RgbImage scaled = scaleNearest(stacked, stacked.w * 8, stacked.h * 8);
        int gridW = tileChars, gridH = tileChars * numTiles;
        int wantW = gridW * 8, wantH = gridH * 8;
        if (scaled.w != wantW || scaled.h != wantH) {
            RgbImage fit(wantW, wantH);
            for (int y = 0; y < wantH; ++y)
                for (int x = 0; x < wantW; ++x) {
                    int sx = (int)(((double)x + 0.5) * scaled.w / wantW);
                    int sy = (int)(((double)y + 0.5) * scaled.h / wantH);
                    if (sx >= scaled.w) sx = scaled.w - 1;
                    if (sy >= scaled.h) sy = scaled.h - 1;
                    int rr, gg, bb; scaled.get(sx, sy, rr, gg, bb); fit.set(x, y, rr, gg, bb);
                }
            scaled = fit;
        }
        IndexImage idx = palettize(scaled, pal);
        payload = imageToPayload(idx, font, pal, gridW, gridH);
        if (r.tile == TileMode::DungNpc1) allBlackToTransparent(payload);
        else objBlackToTransparent(payload, gridW, numTiles, r.tile == TileMode::DungObj0);
        outW = gridW; outH = gridH;
        return true;
    }

    // ---- Screen assets (80x50 or 44x44) ----
    auto loadTransformed = [&](const std::string &file, DecompAlg a,
                               RgbImage &out) -> bool {
        std::vector<uint8_t> comp;
        if (!data.read(file, comp)) { std::fprintf(stderr, "tu4-setup: cannot read %s from %s\n", file.c_str(), data.location().c_str()); return false; }
        std::vector<uint8_t> dec = decompress(comp, a, srcW, srcH);
        if (dec.empty()) { std::fprintf(stderr, "tu4-setup: decompress failed %s\n", file.c_str()); return false; }
        RgbImage im = egaExpand(dec, srcW, srcH);
        if (im.w == 0) { std::fprintf(stderr, "tu4-setup: EGA expand failed %s\n", file.c_str()); return false; }
        int tW, tH;
        if (r.scale > 0) { tW = im.w * r.scale; tH = im.h * r.scale; }
        else fitTarget(srcW, srcH, r.cellsW, r.cellsH, 8, 8, tW, tH);
        if (tW != im.w || tH != im.h) im = scaleNearest(im, tW, tH);
        if (r.cropW > 0 && r.cropH > 0) {
            im = crop(im, r.cropX, r.cropY, r.cropW, r.cropH);
            if (im.w == 0) { std::fprintf(stderr, "tu4-setup: crop failed %s\n", file.c_str()); return false; }
        }
        out = im;
        return true;
    };

    RgbImage img;
    if (!loadTransformed(r.egaFile, r.alg, img)) return false;
    for (const std::string &ov : r.overlays) {
        std::string f = ov; DecompAlg a = r.alg;
        size_t colon = ov.find(':');
        if (colon != std::string::npos) { f = ov.substr(0, colon); a = parseAlg(ov.substr(colon+1), r.alg); }
        RgbImage top;
        if (!loadTransformed(f, a, top)) return false;
        overlayNonBlack(img, top);
    }
    if (img.w != r.cellsW * 8 || img.h != r.cellsH * 8) {
        std::fprintf(stderr, "tu4-setup: %s final image %dx%d != grid %dx%d px\n",
                     r.name, img.w, img.h, r.cellsW * 8, r.cellsH * 8);
        return false;
    }
    IndexImage idx = palettize(img, pal);
    payload = imageToPayload(idx, font, pal, r.cellsW, r.cellsH);
    outW = r.cellsW; outH = r.cellsH;
    return true;
}

// Write BSAVE ASP (7-byte header + payload) — thin wrapper around writeAsp.
static bool writeFinal(const char *path, const std::vector<uint8_t> &payload) {
    return writeAsp(path, payload);
}

// Merge the shipped upper rows 1-8 (bytes 0..1279) over a regenerated payload
// (TITLE hybrid). `shippedAspPath` provides the verbatim upper region.
static bool mergeTitleUpper(std::vector<uint8_t> &payload, const char *shippedAspPath) {
    std::vector<uint8_t> sh;
    if (!readFile(shippedAspPath, sh) || sh.size() < 7 + 1280) {
        std::fprintf(stderr, "tu4-setup: cannot read TITLE upper source %s\n", shippedAspPath);
        return false;
    }
    for (size_t i = 0; i < 1280 && i < payload.size(); ++i) payload[i] = sh[7 + i];
    return true;
}

// ---- Batch driver: regenerate the whole manifest ---------------------------
static int runBatch(const DataSource &data, const char *fontPath,
                    const char *diffsDir, const char *outDir,
                    const char *titleUpperAsp) {
    Font8x8 font;
    if (!loadFont(fontPath, font)) return 1;
    const std::vector<Recipe> &man = egaManifest();
    int ok = 0, fail = 0;

    for (const Recipe &r : man) {
        std::vector<uint8_t> payload; int W, H;
        if (!generatePayload(r, data, font, payload, W, H)) { fail++; std::fprintf(stderr, "  FAIL %s (generate)\n", r.name); continue; }

        // TITLE hybrid: overlay the shipped upper rows 1-8 before the diff.
        if (std::strcmp(r.name, "TITLE") == 0 && titleUpperAsp && *titleUpperAsp) {
            if (!mergeTitleUpper(payload, titleUpperAsp)) { fail++; continue; }
        }

        // Apply the shipped .aspdiff if this asset needs one.
        if (r.hasDiff) {
            std::string dp = std::string(diffsDir) + "/" + r.name + ".aspdiff";
            AspDiff d; std::string err;
            if (!loadAspDiff(dp.c_str(), d, err)) { fail++; std::fprintf(stderr, "  FAIL %s (diff load: %s)\n", r.name, err.c_str()); continue; }
            if (!applyAspDiff(d, payload, err)) { fail++; std::fprintf(stderr, "  FAIL %s (apply: %s)\n", r.name, err.c_str()); continue; }
        }

        std::string op = std::string(outDir) + "/" + r.name + ".ASP";
        if (!writeFinal(op.c_str(), payload)) { fail++; std::fprintf(stderr, "  FAIL %s (write)\n", r.name); continue; }
        ok++;
        std::printf("  OK %-9s %dx%d %s%s\n", r.name, W, H,
                    r.hasDiff ? "+diff" : "", r.tile != TileMode::None ? " (tile)" : "");
    }
    std::printf("tu4-setup batch: %d ok, %d failed (of %zu)\n", ok, fail, man.size());
    return fail == 0 ? 0 : 1;
}

int main(int argc, char *argv[]) {
    const char *dataDir = nullptr;   // NULL => auto-search tu4's data paths
    const char *fontPath = "graphics/converters/cp437_8x8.bin";

    // ---- Batch mode (paths default to installed or dev-tree, resolved below) ----
    bool batch = false;
    const char *diffsDir = nullptr;
    const char *outDir = nullptr;
    const char *titleUpperAsp = nullptr;  // source of TITLE upper rows 1-8

    // ---- Single-asset (CLI) recipe defaults ----
    Recipe cli; cli.name = "OUT"; cli.egaFile = "TREE.EGA"; cli.alg = DecompAlg::Lzw;
    cli.cellsW = 80; cli.cellsH = 50; cli.scale = 0;
    cli.cropW = cli.cropH = cli.cropX = cli.cropY = 0;
    cli.tile = TileMode::None; cli.hasDiff = false;
    const char *outPath = "TREE.ASP";

    std::vector<const char *> pos;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--all")) batch = true;
        else if (!std::strcmp(argv[i], "--data") && i+1 < argc) dataDir = argv[++i];
        else if (!std::strcmp(argv[i], "--font") && i+1 < argc) fontPath = argv[++i];
        else if (!std::strcmp(argv[i], "--diffs") && i+1 < argc) diffsDir = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i+1 < argc) outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--title-upper") && i+1 < argc) titleUpperAsp = argv[++i];
        else if (!std::strcmp(argv[i], "--scale") && i+1 < argc) cli.scale = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--crop") && i+4 < argc) {
            cli.cropW = std::atoi(argv[++i]); cli.cropH = std::atoi(argv[++i]);
            cli.cropX = std::atoi(argv[++i]); cli.cropY = std::atoi(argv[++i]);
        }
        else if (!std::strcmp(argv[i], "--overlay") && i+1 < argc) cli.overlays.push_back(argv[++i]);
        else if (!std::strcmp(argv[i], "--dungobj") && i+1 < argc) cli.tile = std::atoi(argv[++i]) == 0 ? TileMode::DungObj0 : TileMode::DungObj1;
        else if (!std::strcmp(argv[i], "--dungnpc1")) cli.tile = TileMode::DungNpc1;
        else pos.push_back(argv[i]);
    }

    // Resolve the Ultima IV data directory. If --data was not given, search the
    // same locations tu4 itself searches (current dir, ~/.local/share/tu4,
    // /usr/share/tu4, /usr/local/share/tu4, plus Windows drives), for either an
    // unpacked ultima4 dir OR an ultima4.zip — matching the paths advertised in
    // tu4's "data not found" message.
    DataSource data;
    if (!data.open(dataDir /* NULL => auto-search */)) {
        std::fprintf(stderr,
            "tu4-setup: could not find Ultima IV data (an \"ultima4\" folder or\n"
            "ultima4.zip). Put it in one of:\n"
            "  ./ , ~/.local/share/tu4/ , /usr/share/tu4/ , /usr/local/share/tu4/\n"
            "(each searched for ./ultima4, ./u4, or ultima4.zip). Or pass\n"
            "--data <dir-or-zip>.\n");
        return 1;
    }
    std::fprintf(stderr, "tu4-setup: using Ultima IV data at %s\n", data.location().c_str());

    // Resolve batch input/output paths. Prefer the installed setup dir
    // (/usr/share/tu4/setup, from the .deb); fall back to the dev tree. The
    // output defaults to the USER-WRITABLE per-user theme dir (the installed
    // /usr/share is read-only), which tu4 also searches at runtime.
    std::string sFont, sDiffs, sTitle, sOut;
    auto exists = [](const std::string &p){ FILE *f=std::fopen(p.c_str(),"rb"); if(f){std::fclose(f);return true;} return false; };
    auto dirExists = [&](const std::string &p){ return exists(p + "/."); };
    if (batch) {
        const char *INST = "/usr/share/tu4/setup";
        bool installed = dirExists(INST);
        if (!fontPath || !exists(fontPath))
            sFont = installed ? std::string(INST) + "/cp437_8x8.bin"
                              : std::string("graphics/converters/cp437_8x8.bin");
        else sFont = fontPath;
        sDiffs = diffsDir ? diffsDir
               : (installed ? std::string(INST) + "/aspdiff"
                            : std::string("graphics/converters/baselines_EGA/aspdiff"));
        sTitle = titleUpperAsp ? titleUpperAsp
               : (installed ? std::string(INST) + "/title-upper.ASP"
                            : std::string("graphics/EGA/title-upper.ASP"));
        if (outDir) sOut = outDir;
        else {
            const char *home = std::getenv("HOME");
            if (home && home[0]) {
                sOut = std::string(home) + "/.local/share/tu4/graphics/EGA";
                // best-effort create the output dir tree
                std::string cmd = "mkdir -p '" + sOut + "'";
                if (std::system(cmd.c_str()) != 0)
                    std::fprintf(stderr, "tu4-setup: warning: could not create %s\n", sOut.c_str());
            } else sOut = ".";
        }
        fontPath = sFont.c_str(); diffsDir = sDiffs.c_str();
        titleUpperAsp = sTitle.c_str(); outDir = sOut.c_str();
        std::fprintf(stderr, "tu4-setup: writing regenerated EGA assets to %s\n", outDir);
    }

    if (batch)
        return runBatch(data, fontPath, diffsDir, outDir, titleUpperAsp);

    // ---- single-asset path (back-compatible CLI) ----
    if (pos.size() >= 1) cli.egaFile = pos[0];
    if (pos.size() >= 2) cli.alg = parseAlg(pos[1], cli.alg);
    if (pos.size() >= 4) { cli.cellsW = std::atoi(pos[2]); cli.cellsH = std::atoi(pos[3]); }
    if (pos.size() >= 5) outPath = pos[4];

    Font8x8 font;
    if (!loadFont(fontPath, font)) return 1;
    std::vector<uint8_t> payload; int W, H;
    if (!generatePayload(cli, data, font, payload, W, H)) return 1;
    if (!writeFinal(outPath, payload)) { std::fprintf(stderr, "cannot write %s\n", outPath); return 1; }
    std::printf("tu4-setup: wrote %s (%dx%d cells, %zu-byte payload)\n", outPath, W, H, payload.size());
    return 0;
}
