# TU4 Technical Reference

Durable facts and conventions distilled from the tu4 port history
(`changes.txt`). These are the recurring constants, formats, and
gotchas that keep coming up — consult before touching rendering,
layout, assets, or the intro.

## Screen & Layout Constants (text mode, 80×50)

Text mode uses **character coordinates**, not pixels. Key `src/u4.h`
values differ from xu4:

- `BORDER_WIDTH` / `BORDER_HEIGHT` = **2** (was 8)
- `CHAR_WIDTH` / `CHAR_HEIGHT` = **1** (was 8 — positions are characters, not pixels)
- `TILE_WIDTH` / `TILE_HEIGHT` = **4** (tiles are 4×4 characters)
- `TEXT_AREA_X`=48, `TEXT_AREA_Y`=24, `TEXT_AREA_W`=32, `TEXT_AREA_H`=24
- `STATS_AREA_Y`=2, `STATS_AREA_HEIGHT`=16, `STATS_AREA_WIDTH`=30
- `WIND_AREA`: X=18, Y=47, W=10 (centered under map, no border erase)
- Map area is 44×44 chars starting at (2,2); the map viewport is centered inside it.

**Doubling rule:** most areas were doubled from xu4's pixel/tile layout
to fit the 80×50 text screen. When adapting a new layout value, double
the xu4 value as the starting point, then verify coordinate math (see
steering rule 3 — wrapping assumes viewport ≤ 2× map size).

## ASP Asset Format

ASP files are the text-mode equivalent of xu4's PNG/image assets:
- **BSAVE format:** 7-byte header, then payload of **char+attr pairs**
  (1 byte CP437 character + 1 byte EGA color attribute per cell).
- Payload data starts at **offset 7**.
- Tile/charset files are **indexed by Ultima IV save ID (0–255)**, not
  by module tile ID. Convert with `UltimaSaveIds::ultimaId(tile)` before
  indexing into MYSHAPES.ASP / GEM.ASP / etc. (This was the root cause
  of many "wrong graphic" bugs for multi-frame tiles.)
- `loadImage_aspCharset()` takes a **tileCount** argument from the XML
  `tiles` attribute — do not hardcode the payload size. CHARSET.ASP has
  256 entries (512 bytes), GEM.ASP has 128 entries (256 bytes).
- Charset glyphs for **characters < 32** are looked up from CHARSET.ASP
  (char+attr pair at `index*2`). Apply this in every text-drawing path
  (screenMessageN, TextView::drawChar, screenShowChar, cursor).
- Converters live in `graphics/converters/` (see its `README.md` for the
  full list): `aspencode.py` (SHAPES→MYSHAPES), `png2asp_dung.py`
  (352×352 PNG → 44×44 dungeon ASP), `fix_border.py`, etc. Source art
  stays in the sibling theme dirs (`../U5-EGA/`, `../test/`, …); the
  scripts' default paths point back there.

### Subimage coordinates are 1-based
XML subimage `x`/`y` are stored **1-based**. Every place that uses them
as a 0-based array offset needs a **`-1` correction**. This is a
recurring bug class (hit in imageview.cpp, tileview.cpp, intro.cpp).

## Transparency Convention

Tile transparency ("Transparency Hack" setting) is decided at draw time
by inspecting the char+attr:
- **Transparent cell:** space `0x20` + attr `0x00` (skipped when overlaying).
- **Opaque black cell:** full block `0xDB` + attr `0x00` (drawn as solid black).
- NPC/creature tiles (IDs 31–255) use space+0x00 for background cells.
- Terrain tiles (0–30) and field tiles (68–71) use full block+0x00 (opaque).

`drawTile(vector)` iterates layers back (terrain) → front (creature),
enabling transparency after the first layer. Global flag
`g_tileTransparent` is checked in `animDrawTile`, `animDrawTileScroll`,
and the static `drawTile()` path.

## Scroll Animation Direction

Scroll animations must scroll **downward** (matching xu4: source row 0
drawn at destination y=current). Correct formula everywhere:

    (row - offset + H) % H

The buggy upward form was `(row + offset) % H`. Applies to
tileanim.cpp, tileview.cpp, and dungeonview.cpp (fields + fountain).

Note: the 3D dungeon view does **not** go through
`TileAnimTransform::draw()`, so scroll offset advancement must be done
inline in `drawInDungeon()` using `screenState()->currentCycle / 2`.

## Dungeon 3D View

- Each distance level uses a **separate image file** (u5-ega/U5-EGA
  theme layout), not a scaled single image.
- Dungeon ASP files are **44×44 chars** (352×352 PNG at 8px/cell).
- `Image` has a `cols` field (row stride, not hardcoded 80) and a
  `transparent` flag for overlay behavior.
- **Field tiled fill sizes** (match xu4 EGA lscale): dist 0 = 22×22,
  dist 1 = 14×14, dist 2 = 6×6, dist 3 = 3×3, dist 4 = 1×1. Fields
  visible only to distance 2 in some paths (verify against the section).
- Field glyph looked up from GEM.ASP by ultimaId: poison=68, energy=69,
  fire=70, sleep=71.
- **Scaled art files are split by role — objects vs monsters:**
  - `DUNGOBJ0.ASP` (16×16 chars) / `DUNGOBJ1.ASP` (8×8 chars) hold **only
    the 3 objects** (fountain, chest, orb) at indices **0/1/2**.
  - `DUNGNPC1.ASP` (8×8) holds **only monster frames** (11 creatures × 4
    frames = indices 0–43).
  - There is deliberately **no NPC art at distance 0** (no DUNGNPC0):
    monsters are never drawn in the 3D view because combat triggers when
    a creature reaches the adjacent tile, before it would render at
    distance 0. (Config symbols: `dung_obj0`, `dung_obj1`, `dung_npc1`.)
  - `dungNpcTileIndex[]` maps NPCs → `i*4` (0–43) and objects → 44/45/46.
    The OBJ draw paths subtract 44 to index the 0/1/2 file layout; the
    NPC path uses 0–43 directly (with `mt.frame % 4` cycling).
  - EGA theme distances: 0 = DUNGOBJ0 (16×16), 1 = DUNGNPC1 NPCs /
    DUNGOBJ1 objects (8×8), 2 = 4×4 MYSHAPES fallthrough, 3+ = nothing.
    Non-EGA: 0–1 = DUNGOBJ0, 2 = DUNGNPC1/DUNGOBJ1 (8×8), 3+ = fallthrough.
  - `videoType`/`textStyle` is **independent** of the VGA scale tables
    (U5-EGA theme still uses VGA scale).
- **`tiles` XML attribute counts 32-byte (4×4) units**, NOT actual tiles
  (`ASP_TILE_SIZE=32`; loader reads `tiles*32` bytes flat, draw code
  indexes `tileIdx*512` for 16×16 or `tileIdx*128` for 8×8). So a 16×16
  file of N tiles needs `tiles=N*16`; 8×8 needs `tiles=N*4`. ASP tile art
  can be split/trimmed by a **byte-level `dd` slice** (7-byte header +
  fixed-size records) — no PNG regeneration needed.
- **4×4 MYSHAPES fallthrough must honor the tile's `image=` override.**
  Indexing MYSHAPES with `ultimaId(mt)` directly is wrong for tiles that
  have no graphic of their own (e.g. `fountain` — not in
  `tilemap-base.xml` — lands on a garbage/oversized slot → "scrolling
  giant"). xu4 renders these via the `image="tile_<name>"` override
  (fountain→`tile_shallows` = shallows save id 2; magic_orb→
  `tile_magic_flash`; ladders→`tile_bridge`). Resolve: if
  `tile->imageName` is `tile_<name>`, look up that base tile and use its
  `ultimaId`; else fall back to `ultimaId(mt)`.
- **Scroll animation in the 3D view is advanced inline** (the 4×4
  fallthrough and the 8×8/16×16 blocks do NOT go through
  `TileAnimTransform::draw()`): advance `var.scroll.current` from
  `screenState()->currentCycle / 2`, wrap at the tile's row count, and
  read source rows downward via `(row - offset + H) % H`. Key it on the
  tile having an `ATYPE_SCROLL` anim so only scroll tiles (fountain,
  fields, water) animate.

## Intro / Title Animation

- **`updateTitle()` return convention is INVERTED from xu4:** the
  text-mode state machine returns **true when finished**, **false while
  animating**. `timerFired()` must check `if (updateTitle() == true)`.
- The title sequence is a **single blocking call driven from
  `present()`**, NOT from `timerFired()`. Running it from timerFired()
  causes unbounded recursion (IntroController shares the TimedEventMgr
  that titleWait()/wait_msecs() ticks). By the time timerFired() runs,
  mode is already INTRO_MAP or later.
- Per-element pacing mirrors introt.c exactly via `titleWait(msec)`:
  signature 3000/numCoords per coord; BAR 12ms/col; ORIGIN/SUBTITLE
  100ms/step; TITLE scatter 5000ms total; 1000ms pauses between named
  elements; PRESENT 100ms. Do **not** batch multiple draws per tick
  (looks choppy) or use a tick-driven incremental state machine.
- **MAP expand-from-center:** border art lives in **fixed** source
  columns (0–1 left, 78–79 right) of TITLE.ASP — read from fixed source,
  write to moving destination. Clip **per column** (not per whole tile)
  so tiles don't spill past the moving border edge.
- Skip-on-any-key routes through IntroController's own keyPressed()→
  skipTitles() via the `wait_msecs(unsigned, Controller*)` overload.
- Intro text areas widened for 80×50: `INTRO_TEXT_X`=20,
  `INTRO_TEXT_WIDTH`=40 (centered).

## Settings

- `videoType` was renamed to **`textStyle`**; `DEFAULT_VIDEO_TYPE` →
  `DEFAULT_TEXT_STYLE`. Default value is **"U5-EGA"** (formerly
  "9801-style"; directory `graphics/9801-style` → `graphics/U5-EGA`).
- `checkAssets()` derives asset paths from the `textStyle` value at
  runtime — theme directory renames do **not** require a recompile.
- Removed settings that don't apply to 4×4 char tiles: filter, gamma,
  shadow size, shadow opacity. Transparency is on/off only.

## Configure Menu Architecture

- tu4 does **not** redraw BKGD_OPTIONS_TOP/BTM backgrounds (that redraw
  was removed to stop overwriting the persistent title/signature area).
- Menu clear uses `MenuController::redrawMenu()` virtual hook (default =
  `view->clear()` + `menu->show()`). `ReagentsMenuController` overrides
  it to redraw without clearing (preserves the A–H shortcut letters it
  draws separately at column 0). Do not reintroduce `view->clear()` into
  the shared code path.
- Applying Game Graphics Options reloads **all** graphics assets and
  redraws the intro. Without the full reload, a changed `textStyle` keeps
  showing the previously loaded scheme's cached images (stale beasties,
  shapes, backgrounds), because `ImageMgr::notice()` only repoints
  `baseSet` and never frees the loaded `info->image` pixels.
  - **The reload is DEFERRED, not done inside the `USE_SETTINGS` handler.**
    `updateVideoMenu()` only records intent: if `textStyle` changed it sets
    the `IntroController::textStyleChanged` flag (scale/fullscreen still
    call `screenReInit()` inline since they only resize the window). The
    heavy reload must NOT run while the config menu is still open — the
    nested menus (`confMenu` → `videoMenu` → `gfxMenu`) stay open after
    "Use These Settings", and tu4 deliberately does not repaint the
    persistent title/signature background per menu event, so repainting
    mid-menu leaves the title area blank until exit.
  - **The reload runs in `keyPressed()`'s `'c'` handler, after
    `runMenu(&confMenu)` returns** (i.e. back at the main menu, menu fully
    closed). Guarded by `textStyleChanged`, it performs the xu4 cycle
    `deleteIntro(); screenReInit(); init();` plus `titleDataLoadedReset()`,
    then the existing `updateScreen()` repaints the intro (which draws the
    full `BKGD_INTRO`/TITLE.ASP background — title/signature/subtitle
    included — plus menu and beasties) with the new scheme.
  - **`screenReInit()` (text-mode adaptation of xu4)** tears down and
    rebuilds the graphics data: `Tileset::unloadImages()`,
    `delete xu4.imageMgr`, resize the SDL window via
    `screenInit_sys(reset=1)`, then `new ImageMgr` (its ctor re-points
    `baseSet` to the current `textStyle`), rebuild tile anims
    (`newTileAnims`), then `Tileset::loadImages()`. Order matters: tile
    anims are recreated **before** `Tileset::loadImages()` because in text
    mode `Tile::loadImage()` allocates no per-tile pixels — it only
    re-resolves each tile's `anim` pointer from `screenState()->tileanims`,
    so stale anims would dangle otherwise. There is no pixel `screenImage`
    to recreate as in xu4.
  - `titleDataLoadedReset()` clears the file-static `titleDataLoaded` flag
    in intro.cpp so the cached `titleAspData`/`titleReg` (the animated
    title-screen art) is re-read from the new scheme's TITLE.ASP on the
    next `loadTitleData()`.
  - `init()` re-fetches intro graphics (notably the cached `beastiesImg`
    `ImageInfo*`, which would otherwise dangle/point at the old scheme and
    make the beasties fail to draw). It takes the `bSkipTitles` branch
    (titles already ran), so it returns to `INTRO_MENU`, re-sets
    `beastiesVisible=true` / `beastieOffset=0`, and does **not** replay the
    title sequence. **The `'c'` handler MUST set `bSkipTitles = true` before
    calling `init()`** — `bSkipTitles` is only set true by `skipTitles()`,
    NOT when the title animation plays to completion, so if the user let the
    titles finish normally it is still false. Without forcing it, `init()`
    takes the titles branch (`beastiesVisible=false`, `beastieOffset=-32`)
    and the beasties never reappear after the reload. (tu4 has no reason to
    hide beasties in the config menu — the 80×50 layout has room, unlike
    xu4 which suppresses them.)
  - **History / gotcha:** the first revision deliberately did *not* reinit
    on a text-style change (only on scale/fullscreen) — that left assets
    stale. A second revision reloaded *inside* the `USE_SETTINGS` handler
    and tried to repaint the title area while the menu was still open —
    that left the title/signature blank until the menu was exited, and the
    beasties never reappeared. The correct behavior is the DEFERRED reload
    on menu close described above.

## View Highlighting

## View Highlighting

`View::drawHighlighted()` inverts color via **XOR 0xFF on the attribute
byte** (matches xu4's per-channel RGB inversion mapped to EGA 4-bit:
black↔white, blue↔yellow, red↔cyan, etc.), NOT an fg/bg nibble swap.
`highlightActive()` must be reachable outside `#ifdef USE_GL` and
re-applied in `screenUpdate()` during `wait_msecs()` loops.

## Known Latent Bugs (present in xu4 too)

- **`support/SymbolTable.cpp intern()`**: `stringStore.insert(name,
  name+len+1)` copies one byte PAST the token instead of writing a real
  `\0`. Corrupts `symbolName()` lookups. Fix: copy exactly `len` bytes
  then append `\0` explicitly.
- **`Map::moveObjects()` use-after-free**: an attacker creature can be
  destroyed by another creature's ranged attack within the same loop,
  leaving a dangling pointer. Verify the attacker is still in the objects
  deque (`objectPresent()`) before returning it; return NULL otherwise.

## Build (reminder — see tu4-development.md)

Always build from `src/`: `cd /home/adrianus/tu4-1.0/src && make && cp
tu4 ..`. The top-level Makefile only copies the binary; it never
recompiles. Build config: `UI=sdl2` (SDL3_mixer unavailable on this
system), `GPU=none`, `CONF=xml`.
