# Cross-compiling tu4.exe for Windows (from Linux)

This documents how to build the Windows (x86_64) version of tu4 from a
Linux host, as done for the initial `tu4-win32` release. tu4 is the
text-mode SDL2 build (`UI=sdl2 CONF=xml GPU=none`); the Windows build
mirrors the Linux one and renders CP437 characters via SDL2.

The build produces a **fully static** `tu4.exe` — it links SDL2,
SDL2_mixer, libxml2 and zlib statically, so it imports only Windows
system DLLs + the UCRT. The **one** third-party DLL that must ship
alongside it is `libxmp.dll` (see "Music / libxmp" below).

Everything below installs into a **local prefix** (`$HOME/mingw-tu4`) and
needs **no root / sudo**.

---

## 1. Prerequisites on the Linux host

- `curl`, `tar`, `xz`, `7z`/`unzstd` (for extracting packages)
- A native build toolchain + `make` (to cross-build libxml2)
- `imagemagick` (`convert`) — only if regenerating `win32/tu4.ico`

## 2. Toolchain: llvm-mingw (Linux-hosted, targets Windows)

We use **llvm-mingw** — a Linux-hosted MinGW-w64 toolchain (clang) whose
drivers are named `x86_64-w64-mingw32-{gcc,g++,windres,strip}` (so the
Makefile's default `CROSS=x86_64-w64-mingw32-` works unchanged). A GCC
mingw-w64 toolchain (`apt install gcc-mingw-w64-x86-64 …`) also works if
you have root; the Makefile is agnostic.

    mkdir -p ~/mingw-tu4/dl && cd ~/mingw-tu4
    curl -sSL -o dl/llvm-mingw.tar.xz \
      https://github.com/mstorsjo/llvm-mingw/releases/download/20240619/llvm-mingw-20240619-ucrt-ubuntu-20.04-x86_64.tar.xz
    tar xf dl/llvm-mingw.tar.xz
    ln -sfn llvm-mingw-20240619-ucrt-ubuntu-20.04-x86_64 llvm-mingw

    # Verify (should print an ELF path + clang version):
    file llvm-mingw/bin/x86_64-w64-mingw32-clang++
    llvm-mingw/bin/x86_64-w64-mingw32-g++ --version

> NOTE: Do **not** use a Windows-hosted toolchain (e.g. WinLibs) — those
> ship `.exe` binaries that can't run on Linux.

## 3. Dependency sysroot (SDL2, SDL2_mixer, zlib, iconv)

Create one prefix that holds all Windows dev libraries as
`include/ lib/ bin/`:

    SR=~/mingw-tu4/sysroot/mingw64
    mkdir -p "$SR"

### SDL2 + SDL2_mixer (official MinGW dev releases)

    cd ~/mingw-tu4
    curl -sSL -o dl/sdl2.tar.gz \
      https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.tar.gz
    curl -sSL -o dl/sdl2mix.tar.gz \
      https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.0/SDL2_mixer-devel-2.8.0-mingw.tar.gz
    tar xzf dl/sdl2.tar.gz && tar xzf dl/sdl2mix.tar.gz

    # Merge the x86_64-w64-mingw32 prefix into the sysroot:
    cp -r SDL2-2.30.9/x86_64-w64-mingw32/{include,lib}/* "$SR"/ 2>/dev/null
    cp -r SDL2-2.30.9/x86_64-w64-mingw32/include/*  "$SR/include/"
    cp -r SDL2-2.30.9/x86_64-w64-mingw32/lib/*      "$SR/lib/"
    cp -r SDL2-2.30.9/x86_64-w64-mingw32/bin/*.dll  "$SR/bin/"
    cp -r SDL2_mixer-2.8.0/x86_64-w64-mingw32/include/* "$SR/include/"
    cp -r SDL2_mixer-2.8.0/x86_64-w64-mingw32/lib/*     "$SR/lib/"
    cp -r SDL2_mixer-2.8.0/x86_64-w64-mingw32/bin/*.dll "$SR/bin/"

    # Sanity: <SDL2/SDL.h> must resolve under $SR/include
    test -f "$SR/include/SDL2/SDL.h" && echo OK

### zlib + iconv (from MSYS2 packages, extracted only)

    cd ~/mingw-tu4/dl && mkdir -p pkgs && cd pkgs
    BASE=https://mirror.msys2.org/mingw/mingw64
    curl -sSL "$BASE/" -o index.html
    pick() { grep -oE "mingw-w64-x86_64-$1-[0-9][^\"]*-any\.pkg\.tar\.zst" index.html | sort -u | tail -1; }
    for lib in zlib libiconv; do f=$(pick "$lib"); curl -sSL -o "$f" "$BASE/$f"; done
    for f in *.pkg.tar.zst; do tar --use-compress-program=unzstd -xf "$f" -C ~/mingw-tu4/sysroot; done

### libxml2 (cross-built from source — version matters)

tu4's `xml.h` relies on `<libxml/xmlmemory.h>` transitively pulling in
`tree.h` (where `xmlDocPtr`/`xmlNodePtr` are defined). **libxml2 2.9.x**
still does this; **2.11+ / 2.15 do not** and fail to compile with
"unknown type name 'xmlDocPtr'". So build 2.9.14 (matching the version
the Linux build uses) statically:

    export PATH=~/mingw-tu4/llvm-mingw/bin:$PATH
    SR=~/mingw-tu4/sysroot/mingw64
    cd ~/mingw-tu4
    curl -sSL -o dl/libxml2-2.9.14.tar.xz \
      https://download.gnome.org/sources/libxml2/2.9/libxml2-2.9.14.tar.xz
    tar xf dl/libxml2-2.9.14.tar.xz && cd libxml2-2.9.14
    ./configure --host=x86_64-w64-mingw32 --prefix="$SR" \
        --without-python --without-lzma --with-zlib="$SR" --with-iconv="$SR" \
        --enable-static --disable-shared \
        CC=x86_64-w64-mingw32-gcc CXX=x86_64-w64-mingw32-g++ \
        CPPFLAGS="-I$SR/include" LDFLAGS="-L$SR/lib"
    make -j4 && make install

> Because libxml2 is **static**, the tu4 build defines `-DLIBXML_STATIC`
> (already in `Makefile.mingw`) so the headers don't mark symbols
> `__declspec(dllimport)` — otherwise linking fails with
> "undefined symbol: __declspec(dllimport) xml…".

### Music / libxmp

SDL2_mixer plays the game's `.it` (Impulse Tracker) music via **libxmp**,
which it loads *dynamically* (`LoadLibrary`) at runtime. It is not a
link-time import (won't show in `objdump -p tu4.exe`), but without it the
game logs "failed loading libxmp.dll" and plays no music. Fetch the
standalone DLL and drop it into the sysroot `bin/` so the `dist` target
ships it:

    cd ~/mingw-tu4/dl/pkgs
    f=$(grep -oE "mingw-w64-x86_64-libxmp-[0-9][^\"]*-any\.pkg\.tar\.zst" index.html | sort -u | tail -1)
    curl -sSL -o "$f" "https://mirror.msys2.org/mingw/mingw64/$f"
    mkdir -p xmp && tar --use-compress-program=unzstd -xf "$f" -C xmp
    cp xmp/mingw64/bin/libxmp.dll ~/mingw-tu4/sysroot/mingw64/bin/

`libxmp.dll` itself depends only on `KERNEL32` + `msvcrt`, so it is
self-contained.

## 4. Win32 resources (icon + version)

`src/win32/tu4.rc` + `src/win32/tu4.ico` provide the app icon and version
info; `Makefile.mingw` compiles them via `windres` into `tu4res.o`. The
icon was generated from `graphics/tu4.png`:

    convert graphics/tu4.png -filter point -resize 16x16   /tmp/16.png
    convert graphics/tu4.png -filter point -resize 32x32   /tmp/32.png
    convert graphics/tu4.png -filter point -resize 48x48   /tmp/48.png
    convert graphics/tu4.png -filter point -resize 256x256 /tmp/256.png
    convert /tmp/16.png /tmp/32.png /tmp/48.png /tmp/256.png src/win32/tu4.ico

## 5. Build

    export PATH=~/mingw-tu4/llvm-mingw/bin:$PATH

    # IMPORTANT: the cross-build and the native Linux build share src/*.o.
    # Always clean before switching between them, or you'll get ABI/link
    # errors (mixing PE and ELF objects).
    make -C src -f Makefile.mingw clean-local
    rm -f src/*.o src/lzw/*.o src/support/*.o

    make -C src -f Makefile.mingw dist \
        CROSS=x86_64-w64-mingw32- \
        SYSROOT=$HOME/mingw-tu4/sysroot/mingw64 \
        U4PATH=$PWD/tu4-win32

This assembles a runnable folder in `tu4-win32/`:

    tu4-win32/
      tu4.exe            (stripped, ~5.5 MB, fully static)
      libxmp.dll         (tracker-music decoder, loaded at runtime)
      conf/  graphics/{U5-EGA,PC9801}  mid/  sound/
      README.txt  COPYING.txt

> To build only the executable (no dist folder):
> `make -C src -f Makefile.mingw tu4.exe CROSS=… SYSROOT=…`

### Switching back to a native Linux build

    rm -f src/*.o src/lzw/*.o src/support/*.o src/tu4.exe
    make -C src && cp src/tu4 .

## 6. Verify

    export PATH=~/mingw-tu4/llvm-mingw/bin:$PATH
    file tu4-win32/tu4.exe
    # -> PE32+ executable (GUI) x86-64, for MS Windows

    # Should list ONLY Windows system DLLs + api-ms-win-crt-* (no SDL2,
    # libxml2, zlib, SDL2_mixer):
    x86_64-w64-mingw32-objdump -p tu4-win32/tu4.exe | grep -i "DLL Name"

## 7. Run / test under Wine (optional, on the Linux host)

Use a 64-bit Wine prefix (win32 prefixes can't load a PE32+ exe):

    WINEPREFIX=~/.wine64 WINEARCH=win64 wineboot -i   # once, to create it
    cd tu4-win32
    # Ultima IV data must be reachable: put it in an "ultima4" folder here.
    WINEPREFIX=~/.wine64 wine tu4.exe

Settings/saves for that prefix go to
`~/.wine64/drive_c/users/<you>/AppData/Roaming/tu4/`.

If tu4 can't find the Ultima IV data it shows an error dialog pointing to
the free/legal download at <https://www.gog.com/game/ultima_4>.

---

## Notes / gotchas recap

- **Toolchain must be Linux-hosted** (llvm-mingw or apt's mingw-w64), not
  Windows-hosted (WinLibs).
- **libxml2 must be 2.9.x** for tu4's headers; newer majors break the
  compile. Build it static and compile tu4 with `-DLIBXML_STATIC`.
- **Ship `libxmp.dll`** — SDL2_mixer dlopen's it for `.it` music.
- **Clean between cross and native builds** — they share `src/*.o`.
- The static SDL2 link needs a long list of Win32 system libs
  (winmm/setupapi/ole32/ws2_32/…); these are already in `Makefile.mingw`
  `SYS_LIBS` and come from `sdl2.pc`'s `Libs.private`.
