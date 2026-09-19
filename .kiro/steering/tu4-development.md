# TU4 Development Principles

TU4 is a text-mode adaptation of xu4 (Ultima IV Recreated). It renders the game in an 80×50 character display with 16 EGA colors and CP437 charset, instead of xu4's pixel-based graphical output.

## Core Principle

**Copy xu4 as faithfully as possible while adapting it to the text-based user interface.**

## Working Discipline (READ FIRST, EVERY SESSION)

**NEVER make assumptions.** If a fact (a recipe, coordinate, format, geometry,
compression type, crop origin, pipeline step, etc.) is not already written down,
do not guess or derive it by hand and treat the result as truth. Find it in an
authoritative source, verify it against the running code/tools, or ask. State
explicitly when something is unverified.

**At the start of a new session, read ALL of the project documentation before
acting:**
- every `README` (repo root `README.md`, `graphics/converters/README.md`,
  `graphics/converters/png2psci/README.md`, `doc/*.md`, …),
- every `DESIGN` document (e.g.
  `graphics/converters/png2psci/DESIGN-tu4setup.md`),
- `changes.txt`, `todo.txt`, `roadmap.txt`,
- and the `.kiro/steering/*.md` files.

These record the actual pipelines, per-asset recipes, categorizations, and
hard-won gotchas. Much time has been lost reverse-engineering things that were
already documented (e.g. the 44×44 assets are lossless Playscii `.psci`, NOT
matcher-derived; the `png2asp*.py` matchers were removed). Read the docs first;
only then investigate, and prefer authoritative docs over inference.

## Build System

- **Configuration:** `UI=sdl2`, `GPU=none`, `CONF=xml` (set in `make.config`)
- **Always build from `src/` directory:** Run `cd src && make` to compile. The top-level Makefile only does `cp src/tu4 .` and does NOT trigger recompilation.
- **After modifying .cpp/.h files, always build with:** `cd /home/adrianus/tu4-1.0/src && make && cp tu4 ..`
- **If in doubt about stale objects:** Run `cd src && make clean && make` or delete the specific `.o` files before building.
- **Never trust the top-level `make` alone** — it will happily copy a stale binary without recompiling changed source files.

### MANDATORY: any change to a `.h` file requires a full clean rebuild

The Makefile has **no header-dependency tracking**. Incremental `make` only
recompiles the `.cpp` files that changed — it does **not** recompile the other
`.cpp` files that `#include` a modified header. If you edit a header (especially
a struct/class definition like `ImageInfo`, `SubImage`, etc.), an incremental
build links object files compiled against **two different memory layouts** of
the same type. This is silent — the build succeeds with no warning — and causes
**heap corruption at runtime**, not a compile error.

Therefore, **after editing ANY `.h` file:**

    cd /home/adrianus/tu4-1.0/src && make clean && make && cp tu4 ..

Do NOT use a plain incremental `make` after a header change. Symptoms of getting
this wrong (all observed from a real stale-object incident):
- the process spins at **100% CPU** during startup and never finishes init;
- **no window is created** (not just hidden — the SDL window never opens, and
  nothing appears in the taskbar);
- a backtrace shows an infinite loop deep inside an STL container
  (e.g. `std::map::operator[]` / `_Rb_tree`) on a member of the changed struct,
  called from config/asset loading (e.g. `loadImageInfo`).
Seeing `make: Nothing to be done for 'all'` right after a header edit is a red
flag that objects were NOT rebuilt.

### Debugging a hang / "no window" quickly

When the app hangs or no window appears, get a stack trace FIRST — don't guess.
`ptrace_scope=1` blocks attaching to a running process, so launch under gdb and
interrupt the child:

    cd src && gdb -q -batch -ex run -ex bt --args ./tu4   # then SIGINT the tu4 child, or:
    # in another shell:  kill -INT $(pgrep -x tu4)

Also useful: `strace -f -tt ./tu4` (a compute loop shows a long gap with **no
syscalls**); `wmctrl -lp` to see whether a window exists and **which PID owns
it** (beware stale orphan windows from earlier killed runs). Note `validateXml=1`
in `~/.config/tu4/tu4rc` makes libxml2 DTD validation pathologically slow at
startup on this system — the shipping default is `validateXml=0`.

## Reference Implementation

- The original xu4 source is at `/home/adrianus/xu4-1.0`
- Always consult the xu4 source when implementing or fixing features in tu4
- When in doubt about how something should work, check how xu4 does it

## Rules

1. **Preserve xu4's logic exactly** — Do not rewrite algorithms, formulas, or control flow. If xu4 uses a specific constant, coordinate formula, or data structure, replicate it in tu4. Only change what is strictly necessary for text-mode rendering.

2. **Adaptations are limited to the display layer** — The text-mode UI replaces pixel rendering with character+attribute output (screenPutChar). Game logic, coordinate systems, map handling, save files, and all non-rendering code must remain identical to xu4.

3. **When adapting viewport/layout sizes, verify the assumptions of code that uses them** — xu4's algorithms may rely on specific relationships between viewport size and map size (e.g., wrapping math assumes viewport ≤ 2× map size). Changing a layout dimension requires tracing all code paths that consume it to ensure they still produce correct results.

4. **Use xu4's asset format conventions** — Graphics assets are adapted to .ASP format (char+attr pairs) but follow the same indexing and lookup patterns as xu4's PNG/image-based assets. Tile IDs, charset indices, and color attributes must map correctly to their xu4 equivalents.

5. **Do not invent new game behavior** — If xu4 doesn't show something, tu4 shouldn't either. If xu4 shows something a certain way, tu4 should show the text-mode equivalent. The game experience should be the same, just rendered differently.

6. **Test against xu4 behavior** — When a feature seems broken, first verify what xu4 does in the same situation. The xu4 binary at `/home/adrianus/xu4-1.0/src/xu4` and tu4 binary at `/home/adrianus/tu4-1.0/src/tu4` can be compared side by side.

7. **Configuration changes must be validated** — When changing layout parameters in `conf/graphics-text.xml` (viewport sizes, positions, tileshape), trace the rendering code that uses those values to confirm they produce correct output. A viewport that looks right dimensionally may break coordinate math.
