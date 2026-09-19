# tu4-setup Conversion Pipeline

Durable facts for the **tu4-setup** tool (the in-memory C++ port of the
Python `u4dec → PNG → batch_png2psci → psci2asp → ASP` authoring path).
See `graphics/converters/png2psci/DESIGN-tu4setup.md` for the full design.
Module sources live in `src/setup/`; the product binary is `tu4-setup`.

## SCALING: NEAREST / no dither / keep it blocky (CRITICAL)

**All image scaling and palettization in this pipeline must be NEAREST and
NON-DITHERED — keep it blocky. Never introduce a smoothing or dithering
algorithm.**

- Image **scaling** is NEAREST only (`Image.NEAREST` on the Python side;
  integer-ratio block replication in C++). No bicubic/bilinear/Lanczos.
- Image **palettization** must be **pure nearest-color (no dithering)**.
  - PIL `Image.quantize(palette=...)` **defaults to `dither=1`
    (Floyd–Steinberg)**, which sprays stray "phantom" palette indices into
    otherwise-solid regions (e.g. u4dec light-blue (80,80,255) → mostly
    canonical idx 10, but with scattered idx-2 dark-blue pixels). Measured:
    ~2858 stray pixels on TREE at dither=1 vs 0 at dither=0.
  - The C++ matcher uses **pure Euclidean nearest** (`EgaPalette::nearestIndex`),
    which is blocky/non-dithered and is the CORRECT behavior.
  - Therefore the Python baseline must be generated with **`dither=0`
    (`Image.NONE`)** in `palette.py get_palettized_image()` so it matches the
    C++ output. With `dither=0`, PIL quantize == pure Euclidean nearest ==
    C++ `nearestIndex` (verified block-exact, 2026-09-18).
  - **Do not "reproduce PIL dithering" in C++.** The blocky/no-dither result
    is what we want; dithering is an artifact to eliminate at the source.

## Byte-exactness verification (DESIGN §8)

The C++ `png2asp` output must byte-match the **Python `batch_png2psci →
psci2asp` baseline** (ASP_raw = the raw matcher output), NOT the shipped
`.ASP` (ASP_final = raw + manual touch-ups). Compare against a freshly
regenerated baseline, not the shipped asset.

- Shipped assets may carry manual touch-ups captured as a `.aspdiff`
  (DESIGN §6). E.g. **TREE.ASP has authored subimages `moongate` (x=1 y=39
  6×6) and `items` (x=7 y=39 6×6)** per the theme XML — diffing C++ against
  shipped TREE.ASP is NOT a valid matcher gate; regenerate the pure baseline.
- Verification chain for one asset:
  `u4dec <alg> ultima4/NAME.EGA png/NAME.png 320 200`
  → `batch_png2psci.py png/ psci/ --width W --height H --overwrite`
  → `psci2asp.py psci/NAME.psci NAME.baseline.ASP` → `cmp` vs C++ output.
- **DITHER GOTCHA (verified 2026-09-19):** the baseline must be generated with
  PIL quantize `dither=0`. `playscii/palette.py get_palettized_image()` line
  ~170 calls `out_img.quantize(palette=pal_img)` with **NO `dither=` arg**, so
  PIL uses its DEFAULT `dither=1` (Floyd–Steinberg). A default-dithered baseline
  scatters stray palette indices across solid regions and will NOT match the
  C++ pure-nearest matcher (measured: TREE 755/4000 cells differ, INSIDE 954,
  ABACUS raw 2130). To produce a valid gate baseline, temporarily patch that
  call to `quantize(palette=pal_img, dither=Image.NONE)` (do NOT commit the
  edit to the shared playscii install — patch a copy or restore after). The C++
  `tu4-setup` output is byte-identical to the **no-dither** baseline for all 7
  intro assets (TREE, PORTAL, OUTSIDE, INSIDE, WAGON, GYPSY, ABACUS).
- **EGA intro assets are shipped NO-DITHER (2026-09-19).** The 6 assets
  TREE/PORTAL/OUTSIDE/INSIDE/WAGON/GYPSY in `graphics/EGA/` were replaced with
  the no-dither `tu4-setup` output (cleaner; matches the §8 no-dither rule). The
  OLD dithered copies are preserved in `graphics/ega/` (lowercase) and
  `tu4-win32/graphics/EGA/`. **ABACUS in `graphics/EGA/` is STILL the dithered +
  `.aspdiff` touch-up version** (category 2) — its final look comes from the
  hand touch-up, so it was intentionally left unchanged; regenerating it
  no-dither would require re-authoring the touch-up against the no-dither base.
  CONSEQUENCE: the category-1 "regenerate byte-identical" claim below now holds
  only against a **no-dither** baseline for these EGA assets, not the (former)
  dithered shipped bytes.

## Asset parameters (authoritative source: `conf/graphics.xml`)

- Compression per asset is the `filetype`: `image/x-u4lzw` → LZW,
  `image/x-u4rle` → RLE, `image/x-u4raw` → raw. All EGA screens are
  320×200, depth 4 (4bpp).
  - LZW: TREE, PORTAL, OUTSIDE, INSIDE, WAGON, GYPSY, ABACUS, TITLE,
    HONCOM/VALJUS/SACHONOR/SPIRHUM (virtue cards), ANIMATE (beasties).
  - RLE: START (borders), KEY7, RUNE_5 (infinity), STONCRCL, and the
    shrine screens (HONESTY/COMPASSN/VALOR/JUSTICE/SACRIFIC/HONOR/SPIRIT/
    HUMILITY/TRUTH/LOVE/COURAGE — all `fixup="transparent0"`).
- ASP char dimensions come from the shipped payload size (payload=W*H*2):
  - **Full-screen screens = 80×50** (8000-byte payload). Aspect-fit of
    320×200 into 640×400 box = exact **integer 2× NEAREST**. No crop. These
    WERE authored via the batch matcher path (u4dec → batch_png2psci
    --width 80 --height 50 → psci2asp), so the C++ png2asp reproduces them
    byte-exactly (TREE verified). A 320×200 image ALWAYS fits an 80×50 grid
    (exact 2×); feeding it a 44×44 target is invalid (would fractional-scale
    to 352×216 — never do this).
  - **Shrine/rune/codex screens = 44×44** (3872-byte payload). CRITICAL:
    these were **NOT** produced by the u4dec→matcher path. Per
    `graphics/converters/README.md`, the old `png2asp*.py` glyph matchers
    were **removed as obsolete/lossy**; the 44×44 art comes from **Playscii
    `.psci`** converted **losslessly** to `.ASP` via `psci2asp_44.py` (a 1:1
    char/fg/bg → BSAVE copy, no scaling, no crop, no glyph matching). The
    `.psci` may be **pure Playscii conversion output kept as-is, OR manually
    edited in Playscii** (e.g. the documented ladder pink-bug fix) — it is
    NOT guaranteed either way, so do not assume "hand-authored." The `.psci`
    is the source of truth; the shipped `.ASP` is ASP_final, not a
    u4dec-matcher baseline, so do NOT try to byte-reproduce it from U4 data
    with the matcher. Whether a given asset is regenerable is decided by the
    MEASURED diff (DESIGN §6), not by "authored vs converted". (Any earlier
    note about "scale2x + crop 352×352 @ char(3,3)" or a "fractional NEAREST
    follow-up" was based on the DESIGN §1 *plan*, not the actual authoring.)
  - Layout note for a FUTURE non-80×50 theme (roadmap "Alternate screen and
    tile sizes"): coordinates are **1-based**; with `BORDER_WIDTH=2`, content
    begins at char (3,3). Any crop/offset must be expressed as a layout
    formula (`(BORDER_WIDTH)*CELL_PX`, etc.), not a hardcoded pixel literal,
    so it survives a 5×5-tile / 80×60 or 100×60 layout.

## Asset categorization (MEASURED, DESIGN §6 — do NOT re-derive)

Which files are regenerated from Ultima IV data vs shipped as-is was decided in
a prior session by MEASURING each asset's diff (fresh matcher output vs shipped
ASP), NOT by reasoning about how it was authored. The four categories:

1. **Regenerate byte-identical (NO diff):** TREE, GYPSY, INSIDE, WAGON, PORTAL,
   OUTSIDE, DUNGOBJ0, DUNGOBJ1, DUNGNPC1, RUNE_0..4, codex chars, KEY7 (~21B).
   (NOTE 2026-09-19: for the intro screens TREE/GYPSY/INSIDE/WAGON/PORTAL/
   OUTSIDE "byte-identical" is against the **no-dither** baseline; the EGA theme
   now ships the no-dither versions. See the DITHER GOTCHA + no-dither note in
   the byte-exactness section above.)
2. **Regenerate + shipped `.aspdiff` (small touch-ups):** virtue cards
   (~260–300B), ANIMATE (509B), START (706B), ABACUS (910B), RUNE_5 (78B),
   codex composites (36–423B), and **TITLE (hybrid — see below)**.
3. **Ship directly, NO regen (own art):** MYSHAPES, MOONPHAS, CHARSET, GEM.
4. **Ship under xu4 GPLv3 (xu4 art, no U4 source):** dungeon walls/doors/
   ladders/traps (dungeonhall*, dung0-3ma*, ladder*, TRAPS).

> CORRECTION (2026-09-18, per project owner): an earlier note placed
> **DUNGOBJ1** under category 4 ("no U4 source"). That is WRONG — both
> DUNGOBJ0 and DUNGOBJ1 are REGENERATED from `SHAPES.EGA` (see the DUNGOBJ
> recipe below). Only the wall/door/ladder/trap art has no U4 source.

### DUNGOBJ0 / DUNGOBJ1 — object tiles cropped from SHAPES.EGA
Both are U4-derived: the object tiles are cropped out of `SHAPES.EGA` (256
tiles, 16×16px, 4bpp, high-nibble-first) by Ultima IV save ID, then arranged
into a horizontal object sheet (`scobjsrc.png`), converted PNG→`.psci`→
screen-layout ASP, and DE-INTERLEAVED into sequential tiles by
`aspencode_dungobj0.py` (16×16 tiles: 64×16 screen → 4×512, tile N at 7+N*512)
/ `aspencode_dungobj1.py` (8×8 tiles: 32×8 screen → 4×128, tile N at 7+N*128).
Object tiles + their SHAPES save IDs (confirmed from `conf/tileset-base.xml`
image= overrides + `tile_transparency.md`):
- **fountain** → `tile_shallows` = **save id 2** (fountain has no own graphic)
- **chest** → **save id 60**
- **orb** (`magic_orb`) → `tile_magic_flash` = **save id 78**
- **altar** → **save id 74** (DUNGOBJ0 only — it has 4 tiles; DUNGOBJ1 has 3)
Per-object transparency step (below) applies: Playscii flattens transparent PNG
pixels to char 0x00/attr 0x00, which the per-object rule then turns into space
0x20 (transparent) or leaves opaque, depending on tile type.

### Transparency step for regenerated tile art (per-object rule, no diff)
Playscii `convert('RGB')` flattens transparent PNG pixels to black, so the raw
matcher output marks a "black cell" as char `0x00` AND attr `0x00`. A naive
"all black → space" (the old `asp_blackfill_to_space.py`) is WRONG for the
DUNGOBJ0 chest, whose black body edge must stay opaque. The IMPLEMENTED rule
(project owner, 2026-09-18/19; `src/setup/dungobj.cpp objBlackToTransparent`)
is a **per-object-type rule keyed on the tile's stack index**, NOT a flood
fill. Tile order (both sheets): 0=fountain, 1=chest, 2=orb, 3=altar (obj0 only).
Encoding matches the engine's runtime convention (`dungeonview.cpp`:
`if (ch == ' ') continue;`): **transparent = char `0x20`, attr `0x00`
(skipped by the renderer); opaque = the cell left as the matcher produced it**
(black opaque = char `0x00`/attr `0x00`, drawn as a solid black char). There is
NO `0xDB` block and NO `.aspdiff` — the rule is fully deterministic:

- **fountain (0):** all black → **opaque** (leave unchanged).
- **chest (1):**
  - **DUNGOBJ0** (16×16): a black cell → **opaque** iff it has **≥ 1 non-black
    8-connected neighbour**, else **transparent**. Off-tile neighbours (outside
    THIS tile's rows/cols) count as **BLACK**. The neighbour test reads a
    snapshot of the ORIGINAL black map taken before any cell is mutated, so a
    cell turned transparent earlier cannot be miscounted.
  - **DUNGOBJ1** (8×8): all black → **transparent** (the small chest has no
    opaque-black body to preserve at this size).
- **orb (2):** all black → **transparent**.
- **altar (3, DUNGOBJ0 only):** all black → **transparent**.

Applies to the REGENERATED object tile art (DUNGOBJ0, DUNGOBJ1). Monster tiles
(DUNGNPC1) have no opaque interior at all, so they use the blanket
`allBlackToTransparent` (every black cell → space `0x20`). The codex/rune/virtue
composites do NOT need any of this — their composition happens at the PNG stage
before ASP.

> HISTORICAL NOTE: an earlier design used a per-tile 4-connected flood fill from
> the tile border (exterior black → space, interior black → `0xDB` block) plus a
> shipped `.aspdiff` touch-up. That approach was REPLACED by the per-object rule
> above; no DUNGOBJ `.aspdiff` is shipped and no `0xDB` is emitted.

### DUNGOBJ0 / DUNGOBJ1 — complete generation recipe (confirmed 2026-09-18)
Both regenerated from `SHAPES.EGA` (U4 data). SAME source sheet for both; only
the target char-tile size differs.
1. Decode SHAPES.EGA → 256 tiles, 16×16 px, 4bpp (2 px/byte, high nibble left).
2. Crop the object tiles by save ID and **stack them VERTICALLY** (they get
   serialized sequentially anyway):
   - DUNGOBJ0: fountain(id 2), chest(id 60), orb(id 78), altar(id 74) — 4 tiles.
   - DUNGOBJ1: fountain(id 2), chest(id 60), orb(id 78) — 3 tiles.
3. **Scale 8× NEAREST, dither=None** → each 16×16-px tile becomes 128×128 px
   (= 16×16 chars at 8 px/char). This scaled sheet is the source for BOTH.
4. PNG → `.psci`: **same 128×128-px source**, only the target char grid differs
   — DUNGOBJ0 = 16×16-char tiles, DUNGOBJ1 = 8×8-char tiles. (DUNGOBJ1 is NOT a
   separate 4× scale; it's the same 8× pixel source matched to a smaller grid.)
5. `.psci` → screen-layout ASP.
6. **Per-object transparency** (above): fountain all-opaque; chest obj0 =
   ≥1 non-black 8-nbr → opaque else transparent (off-tile = black), chest obj1
   = all black → transparent; orb/altar all black → transparent. Transparent =
   space `0x20`; opaque cells left unchanged (no `0xDB`, no `.aspdiff`).
7. **No serialize/de-interleave step needed** — because the tiles were stacked
   VERTICALLY in step 2, the matcher emits cells top-to-bottom in exactly the
   engine's sequential-tile order (all of tile 0's rows, then tile 1's, …), so
   the ASP is already tile N at offset `7 + N*tileBytes`
   (DUNGOBJ0 512, DUNGOBJ1 128). The old `aspencode_dungobj*.py` de-interleave
   was ONLY for the previous HORIZONTAL (side-by-side) layout, where screen
   rows interleaved the tiles. Vertical stacking eliminates it.

### DUNGNPC1 — monster tile art from SHAPES.EGA (verified byte-exact 2026-09-18)
Same pipeline as DUNGOBJ, 8×8-char tiles, **44 tiles = 11 creatures × 4
consecutive frames**. Crop from SHAPES.EGA by save ID, stack VERTICALLY, scale
8× NEAREST no-dither, match at 8×8-char tiles, blanket all-black→space `0x20`
transparency (monster tiles have no opaque interior, so NO per-object rule),
NO serialize (vertical order is already sequential). Payload = 44×128 = 5632 B.
Creature base save IDs (steering + `tile_transparency.md`), each +0..+3 frames,
**skipping mimic (172–175)**; order matches `dungNpcTileIndex[]` (NPC i → i*4):
  rat 144, bat 148, spider 152, ghost 156, slime 160, troll 164, gremlin 168,
  reaper 176, insect_swarm 180, gazer 184, phantom 188.
`tu4-setup SHAPES.EGA raw 0 0 OUT.ASP --dungnpc1` produces it; verified
BYTE-IDENTICAL to the Python baseline.

### TITLE.ASP — HYBRID generation recipe (verified geometry 2026-09-18)
TITLE.ASP is 80×50 (8007 bytes = 7-byte header + 8000-byte payload, 2 bytes/
cell). It is split by ROW (rows are 1-based):
- **Upper region = rows 1–8** (0-based rows 0–7 = payload bytes **0..1279**):
  shipped **pre-populated in the base ASP**. NOT regenerated from U4 data.
- **Lower region = rows 9–50** (0-based rows 8–49 = payload bytes
  **1280..7999**): regenerated from Ultima IV data.

Pipeline to produce the final TITLE.ASP on the user's machine (VERIFIED
byte-exact 2026-09-19):
1. `u4dec` TITLE.EGA → PNG (LZW, 320×200) → matcher → raw 80×50 ASP.
2. **Build the merged raw:** copy the shipped upper rows 1–8 (payload bytes
   0..1279) OVER the matcher output's upper rows, keeping the matcher's lower
   rows 9–50. (The upper region is the hand-authored Lord British signature /
   ORIGIN logo the matcher cannot reproduce — 318 upper cells differ — so it is
   taken verbatim from the shipped ASP, NOT regenerated.) Merge is at the **ASP
   byte level**.
3. **Align** the merged raw's lower region to the intended (shipped) baseline on
   pixel-identical look-alike cells (align_asp; ~932 look-alikes adopted), then
4. **Apply the shipped TITLE `.aspdiff`** to the merged+aligned raw → final
   byte-exact ASP. MEASURED: the diff is **377 changed cells / 1526 B, ALL in
   the lower region rows 9–50 (0 records in rows 1–8)** — confirming the diff
   scope is the generated lower region only. (An earlier ~598B estimate was a
   guess; the measured value is 1526B for the aligned baseline.)

The upper 8 rows need no diff (shipped verbatim); the diff scope is the
generated lower region only. `mkaspdiff` applies NO transform (the align step
is separate and explicit); apply = plain record-apply + CRC32-of-final check.

### Codex / rune / shrine 44×44 screens — generation recipe (confirmed 2026-09-18)
Applies to: KEY7, the 8 virtues (HONESTY, COMPASSN, VALOR, JUSTICE, SACRIFIC,
HONOR, SPIRIT, HUMILITY), the 3 principles (TRUTH, LOVE, COURAGE), STONCRCL,
and RUNE_0..5. Each has its own 320×200 `.EGA` in `ultima4/` (all
`image/x-u4rle`, verified present). These ARE regenerated from U4 data (they
are NOT "hand-authored only") via:

1. `u4dec` <NAME>.EGA → 320×200 PNG (RLE).
2. **scale2× → 640×400** (exact integer NEAREST; a 320×200 image never fits a
   44×44 grid directly — feeding it straight to a 44×44 target would
   fractional-scale to 352×216, which is WRONG).
3. **crop 352×352 @ char(3,3)** = pixel **(16,16)** on the 640×400 image.
   (1-based char 3 with `BORDER_WIDTH=2` → `(3-1)*8 = 16`. Express as a layout
   formula, not the literal 16, for future non-80×50 themes.)
4. **cumulative OR-overlay** for the codex endgame "build-up" composites
   (key → +triangle → +star → +circled sigil → +inner circles). The overlay
   may be applied before OR after the crop — the end result is identical; the
   only requirement is that it is done **before converting to ASP**.
   - **Overlay/display order (confirmed from `src/codex.cpp` +
     `savegame.h`/`imagemgr.h`, NOT assumed):** the codex draws
     `codexImageNames[current]` where `codexImageNames = &BKGD_HONESTY`, so the
     BKGD_ symbols are a contiguous run indexed by `Virtue` then `BaseVirtue`:
     KEY7 first, then the **8 virtues in `Virtue` enum order** — HONESTY,
     COMPASSN, VALOR, JUSTICE, SACRIFIC, HONOR, SPIRIT, HUMILITY — then the
     **3 principles in `BaseVirtue` bit order** (0x01/0x02/0x04) — TRUTH, LOVE,
     COURAGE — and finally STONCRCL (drawn in `codexHandleEndgame`). RUNE_5
     (infinity, `BKGD_RUNE_INF`) is shown in `codexHandleInfinity`.
   - NOTE: the game displays each *complete* composite (one full image per
     step via `screenDrawImageInMapArea`), NOT a runtime incremental overlay —
     so the cumulative build-up is baked into each successive source PNG at
     authoring time, in the order above.
5. `batch_png2psci` at **44×44** (dos40/ega) → `.psci` (352 = 44×8, so this is
   an exact 1× match, no further scaling) → `psci2asp_44.py` → `.ASP`.
6. Apply the shipped `.aspdiff` where the MEASURED diff is non-zero (per the
   §6 categories: RUNE_0..4/KEY7 = byte-identical/no diff; RUNE_5 ~78B; codex
   composites 36–423B).

NOTE the geometry contrast with 80×50 full-screen assets: a 320×200 image
→ 80×50 is an exact 2× with NO crop (TREE etc.), whereas → 44×44 REQUIRES the
scale2×+crop-to-352×352 step above. Do not confuse the two paths.

## EGA → RGB expansion (the byte-exactness crux)

- `u4dec` bakes EGA indices into PNGs via pngconv.c `setEgaPalette()`, whose
  RGB values are NOT canonical EGA — they use deltas: blue (0,0,**162**),
  lt-gray (**168,168,168**), dk-gray (**82,82,82**), lt-blue (80,80,**255**),
  brown (170,85,0). `src/setup/decode.cpp` `egaExpand()` uses these EXACT
  values (table `kU4decEga`) so the in-memory RGB equals the authoring PNG.
- `EgaPalette::nearestIndex()` then snaps each u4dec RGB to a canonical EGA
  index (deltas are 2..8, unambiguous, no ties), reproducing PIL nearest.
- 4bpp layout: 2 px/byte, **high nibble = left pixel** (libpng MSB-first).

## Playscii palette facts (verified from `ega.png`, 2026-09-18)

- `colors[]` = 17 entries: slot 0 = transparent (0,0,0,0); slots 1..16 =
  canonical EGA 0..15. `darkest_index = 1` (black), `lightest_index = 16`
  (white). `src/setup/ega_palette.cpp` matches this exactly.
- `update()` post-step: `fg = darkest_index if fg==0`; `bg = darkest_index
  if bg==0` (both → 1).
- psci→ASP: `fg_ega = playscii_fg - 1`, `bg_ega = playscii_bg - 1`,
  `attr = (bg_ega<<4)|fg_ega`. BSAVE header
  `struct.pack('<BHHH', 0xFD, 0xB800, 0x2040, W*H*2)`.

## Glyph matcher float precision

`png2asp.cpp matchBlock` sums the 64 per-pixel L*a*b* diffs using numpy's
**float32 pairwise summation** (8-accumulator unrolled loop, combined as
`((r0+r1)+(r2+r3))+((r4+r5)+(r6+r7))`), NOT a naive left-to-right sum and NOT
`double`. Genuine glyph ties differ only in the last FP bit; the summation
order decides the winner. This is REQUIRED for byte-exact glyphs — but it is
only meaningful once the palettized INPUT block matches (see no-dither rule
above); a wrong input block changes the glyph regardless of summation.

## Manifest batch driver (regenerate the whole EGA theme)

`tu4-setup` has a per-asset manifest (`src/setup/manifest.{h,cpp}`,
`egaManifest()`) mapping every regenerable EGA asset to its Recipe
(EGA source, compression, char grid, scale, crop, cumulative overlays,
tile-mode, and whether a `.aspdiff` applies). The batch mode iterates it:

    tu4-setup --all --data ultima4 --font graphics/converters/cp437_8x8.bin \
      --diffs graphics/converters/baselines_EGA/aspdiff \
      --out <OUTDIR> --title-upper graphics/EGA/TITLE.ASP

For each asset it regenerates the raw payload (screen or SHAPES-tile path),
then — for the hybrid TITLE — merges the shipped upper rows 1-8 from
`--title-upper`, then applies `<name>.aspdiff` from `--diffs` if the recipe has
one, and writes `<OUTDIR>/<name>.ASP`. VERIFIED 2026-09-19: all 32 manifest
assets come out **byte-identical to `graphics/converters/baselines_EGA/`**
(category-1 directly; category-2 = raw + `.aspdiff`; TITLE = merged + diff).
The single-asset CLI (`tu4-setup NAME.EGA alg W H OUT.ASP [--scale/--crop/
--overlay/--dungobj/--dungnpc1]`) still works for one-off regen/verification;
both paths call the SAME `generatePayload(Recipe, ...)`.

## Build (tu4-setup target)

- Compile `src/setup/*.cpp` + reuse rle/lzw from source. **Do NOT reuse the
  checked-in `rle.o` / `lzw/*.o`** — they may be stale/cross-compiled
  (observed MinGW/Windows objects → "dangerous relocation" / undefined
  `probe1/2/3`). Compile `rle.cpp`, `lzw/lzw.c`, `lzw/hash.c` fresh natively.
- `rle.h` uses `FILE*` without including `<cstdio>` — include `<cstdio>`
  BEFORE `../rle.h` in any TU that pulls it in.
- `image_ops.h` needs `<cstddef>` for bare `size_t` (only `<cstdint>` is not
  enough on this libstdc++: it exposes `std::size_t`, not global `::size_t`).

## VERIFIED OUTCOME (2026-09-19) — supersedes earlier speculation

The C++ tu4-setup matcher was verified **byte-identical to the Python
batch_png2psci→psci2asp matcher** for every asset tested (TREE, all codex/rune,
ANIMATE, DUNGOBJ0/1, DUNGNPC1). So the C++ path is a faithful reproduction of
the Python authoring path — NOT a separate approximation.

CONTRADICTION RESOLVED: an earlier passage above claims the 44×44 shrine/rune/
codex screens are "NOT produced by the u4dec→matcher path" (lossless Playscii
`.psci` only). That is WRONG for the EGA theme. MEASURED: the 44×44 codex/rune
screens ARE regenerated from their U4 `.EGA` via u4dec→scale2×→crop 352×352 @
(16,16)→matcher, and the C++ output is byte-identical to a fresh Python baseline
(RUNE_0..5, KEY7, STONCRCL, HONESTY..COURAGE all verified). Trust THIS section
and the "Codex / rune / shrine 44×44 recipe" over the older Playscii-only note.

### EGA color: gray, not white
Several shipped `graphics/EGA/*.ASP` (the codex screens, START, ANIMATE) were
found to be **wrong-theme copies of the U5-EGA (white) art**, byte-identical to
the U5-EGA files. For the **EGA theme the correct rune/codex color is GRAY**
(u4dec decodes the rune as (168,168,168) → canonical EGA 7; the matcher is
correct). The `baselines_EGA/` copies hold the correct GRAY conversion; the
shipped white files should eventually be replaced (separate shipped-asset fix).

### baselines_EGA/ final state (dev-only verification set, NOT shipped)
- **Category-1 (conversion == baseline, NO diff):** TREE, PORTAL, OUTSIDE,
  INSIDE, WAGON, GYPSY, DUNGOBJ0, DUNGOBJ1, DUNGNPC1, KEY7, STONCRCL, and the
  8 virtues + 3 principles (HONESTY..COURAGE). Deterministic; store raw
  conversion output.
- **Category-2 (baseline + `.aspdiff`, round-trip BYTE-EXACT):** RUNE_0..5
  (shrine touch-ups), START (hand-edited), ABACUS (hand-edited), ANIMATE
  (matcher-faithful raw + hand-cleaned border touch-up), TITLE (hybrid: shipped
  upper rows 1–8 + matcher lower + lower-only diff). Diffs live in
  `baselines_EGA/aspdiff/`.

### `.aspdiff` tooling (standard-characters step is SEPARATE, per owner)
`mkaspdiff` and `applyAspDiff` apply NO transform: mkaspdiff is a plain
raw-vs-final sparse patch (record every differing cell, store final bytes, CRC32
of final); apply is plain record-apply + CRC32-of-final check. Any normalization
(e.g. `allBlackToTransparent` for tile art, or align_asp adopting look-alike
cells) is a SEPARATE explicit step run on the inputs BEFORE diff/apply. The
align-then-diff workflow (align_asp adopts pixel-identical look-alike cells into
the baseline, then mkaspdiff captures only genuine visible touch-ups) is the
standard way to keep diffs minimal.
