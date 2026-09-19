#!/bin/sh
# Build a Debian (.deb) package for TU4.
#
# Mirrors the layout of packaging/setup (install action):
#   /usr/bin/tu4
#   /usr/share/tu4/{conf,conf/themes,graphics/EGA,mid,sound}
#   /usr/share/applications/tu4.desktop
#   /usr/share/icons/hicolor/48x48/apps/tu4.png
#
# Only runtime assets are packaged. The text-mode theme ships ONLY its
# .ASP files (plus cp437_8x8.bin and palette.bin); leftover source art
# (*.png, *.vga, *.ori, *.bak, *.psci) is intentionally excluded.
#
# graphics-text.xml is an aggregator that XIncludes per-theme fragments
# from conf/themes/. The base package ships ONLY EGA (the default).
# U5-EGA and PC9801 are future add-on modules and are NOT packaged here.
#
# Ultima IV DOS game data is NOT included (it is not redistributable and
# must be supplied by the user at runtime).
set -eu

SELF_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT_DIR=$(cd "$SELF_DIR/.." && pwd)

VERSION=${VERSION:-1.0}
ARCH=$(dpkg --print-architecture)
PKG=tu4
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

RES=/usr/share/tu4

if [ ! -x "$ROOT_DIR/tu4" ]; then
	echo "Error: $ROOT_DIR/tu4 not found. Build it first: (cd src && make && cp tu4 ..)" >&2
	exit 1
fi

# ---- directory skeleton ------------------------------------------------
install -d "$STAGE/DEBIAN"
install -d "$STAGE/usr/bin"
install -d "$STAGE$RES/conf" "$STAGE$RES/conf/dtd" "$STAGE$RES/conf/themes"
install -d "$STAGE$RES/graphics/EGA"
install -d "$STAGE$RES/mid" "$STAGE$RES/sound"
install -d "$STAGE/usr/share/applications"
install -d "$STAGE/usr/share/icons/hicolor/48x48/apps"

# ---- binary ------------------------------------------------------------
install -m 755 "$ROOT_DIR/tu4" "$STAGE/usr/bin/tu4"

# ---- config ------------------------------------------------------------
# conf/*.xml matches the loose top-level configs (graphics-text.xml is the
# aggregator). The *.xml glob does NOT match conf/graphics-text.xml.presplit.bak
# or the conf/themes/ subdirectory, so both are handled separately.
install -m 644 "$ROOT_DIR"/conf/*.xml "$STAGE$RES/conf"
install -m 644 "$ROOT_DIR"/conf/sigdata.txt "$STAGE$RES/conf"
install -m 644 "$ROOT_DIR"/conf/dtd/*.dtd "$STAGE$RES/conf/dtd"
# Per-theme fragment XIncluded by graphics-text.xml. Ship only the base
# theme (EGA, the default). U5-EGA.xml and PC9801.xml are reserved for future
# add-on modules and are NOT packaged here.
install -m 644 "$ROOT_DIR"/conf/themes/EGA.xml "$STAGE$RES/conf/themes"

# ---- graphics themes: ASP files only (omit PNG and other source art) ---
# copy_asp <src-theme-dir> <dst-theme-dir>
copy_asp() {
	src=$1; dst=$2
	# .ASP tile/charset/dungeon assets
	for f in "$src"/*.ASP; do
		[ -e "$f" ] || continue
		install -m 644 "$f" "$dst"
	done
	# charset binary needed by the U5-EGA theme, if present
	if [ -e "$src/cp437_8x8.bin" ]; then
		install -m 644 "$src/cp437_8x8.bin" "$dst"
	fi
	# per-theme 16-colour palette (48 bytes RGB), if present
	if [ -e "$src/palette.bin" ]; then
		install -m 644 "$src/palette.bin" "$dst"
	fi
}
copy_asp "$ROOT_DIR/graphics/EGA" "$STAGE$RES/graphics/EGA"

# window/taskbar icon used at runtime
install -m 644 "$ROOT_DIR/graphics/tu4.bmp" "$STAGE$RES/graphics/tu4.bmp"

# ---- music & sound -----------------------------------------------------
for f in "$ROOT_DIR"/mid/*.mid; do [ -e "$f" ] && install -m 644 "$f" "$STAGE$RES/mid"; done
for f in "$ROOT_DIR"/mid/*.it;  do [ -e "$f" ] && install -m 644 "$f" "$STAGE$RES/mid"; done
for f in "$ROOT_DIR"/sound/*.ogg; do [ -e "$f" ] && install -m 644 "$f" "$STAGE$RES/sound"; done

# ---- desktop integration ----------------------------------------------
install -m 644 "$ROOT_DIR/graphics/tu4.png" \
	"$STAGE/usr/share/icons/hicolor/48x48/apps/tu4.png"

# desktop launcher: rewrite the dev absolute paths to the installed ones
sed -e 's#^Exec=.*#Exec=tu4#' \
    -e '/^Path=/d' \
    "$ROOT_DIR/packaging/tu4.desktop" > "$STAGE/usr/share/applications/tu4.desktop"
chmod 644 "$STAGE/usr/share/applications/tu4.desktop"

# ---- control file ------------------------------------------------------
INSTALLED_KB=$(du -sk "$STAGE" | cut -f1)
cat > "$STAGE/DEBIAN/control" <<EOF
Package: $PKG
Version: $VERSION
Section: games
Priority: optional
Architecture: $ARCH
Depends: libc6, libsdl2-2.0-0, libsdl2-mixer-2.0-0
Installed-Size: $INSTALLED_KB
Maintainer: ASCII Dragon <tu4@localhost>
Homepage: https://xu4.sourceforge.net/
Description: Text-mode (80x50 CP437) recreation of Ultima IV
 TU4 is a text-mode adaptation of xu4, rendering Ultima IV in an
 80x50 character display using the 16-color EGA palette and the CP437
 character set through an SDL2 backend.
 .
 Ultima IV for DOS game data is required at runtime and is NOT included
 in this package (it is available as freeware). Place the data where TU4
 can find it, e.g. ~/.local/share/tu4/ultima4. See the README for
 details.
EOF

# ---- postinst / postrm: refresh icon + desktop caches ------------------
cat > "$STAGE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database -q /usr/share/applications || true
	fi
	if command -v gtk-update-icon-cache >/dev/null 2>&1; then
		gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
	fi
fi
EOF
chmod 755 "$STAGE/DEBIAN/postinst"

cat > "$STAGE/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
	if command -v update-desktop-database >/dev/null 2>&1; then
		update-desktop-database -q /usr/share/applications || true
	fi
fi
EOF
chmod 755 "$STAGE/DEBIAN/postrm"

# ---- build -------------------------------------------------------------
OUT="$ROOT_DIR/${PKG}_${VERSION}_${ARCH}.deb"
fakeroot dpkg-deb --build "$STAGE" "$OUT" >/dev/null
echo "Built: $OUT"
