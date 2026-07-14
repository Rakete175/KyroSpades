#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# make_icons.sh <source.png>  — generate iOS app-icon PNGs into ios/icons/.
#
# iOS app icons MUST be opaque (no alpha) and square. Transparency renders as
# white/black fill on the home screen, which is the "white planet on white" bug.
# Point this at the SAME opaque, black-background icon you made for Android
# (a single large square PNG, ideally >= 1024x1024).
#
# Uses only macOS's built-in `sips`. If the source still has an alpha channel,
# and ImageMagick (`magick`) is installed, we flatten it onto black first;
# otherwise we assume the source is already opaque.
#
# Output filenames match the CFBundleIcons entries in ios/Info.plist.
# ─────────────────────────────────────────────────────────────────────────────
set -euo pipefail

SRC="${1:-}"
if [ -z "$SRC" ] || [ ! -f "$SRC" ]; then
	echo "usage: $0 <source-square-opaque.png>" >&2
	exit 1
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="$ROOT/icons"
mkdir -p "$OUT"

# Flatten alpha onto black if we can detect it and ImageMagick is available.
FLAT="$SRC"
if command -v magick >/dev/null 2>&1; then
	FLAT="$OUT/.flattened.png"
	magick "$SRC" -background black -alpha remove -alpha off "$FLAT"
fi

# name:size pairs. base (no suffix) = @1x; @2x/@3x are separate files iOS picks
# by device scale. iPhone home needs 120 (@2x) + 180 (@3x); iPad adds 152/167.
gen() { # px outfile
	sips -s format png -z "$1" "$1" "$FLAT" --out "$OUT/$2" >/dev/null
}

gen 120 "AppIcon60x60@2x.png"     # iPhone home @2x
gen 180 "AppIcon60x60@3x.png"     # iPhone home @3x
gen 76  "AppIcon76x76.png"        # iPad home @1x
gen 152 "AppIcon76x76@2x.png"     # iPad home @2x
gen 167 "AppIcon83.5x83.5@2x.png" # iPad Pro home
gen 1024 "AppIcon1024.png"        # marketing / App Store (harmless to include)

[ "$FLAT" != "$SRC" ] && rm -f "$FLAT" || true

echo "Wrote icons to $OUT:"
ls -1 "$OUT"
echo ""
echo "Now re-run ./build_ios.sh — it copies ios/icons/*.png into the bundle."
