#!/usr/bin/env bash
#
# package_android_assets.sh
# -------------------------
# Assembles the KyroSpades game assets into the Android Studio project's
# assets/ folder so SDL_RWFromFile (which reads from APK assets on Android)
# can find config.ini, fonts/, png/, kv6/, wav/, etc.
#
# Merge order (later overwrites earlier):
#   1. resources/            (config.ini, fonts, base png + kv6)
#   2. bsresources.zip       (third-party kv6/png/wav — the big pack;
#                             downloaded automatically if not present)
#   3. resources.override/   (project overrides — win over everything)
#
# Usage:
#   ./package_android_assets.sh [KYROSPADES_DIR] [ANDROID_PROJECT_DIR]
#
# With no arguments it assumes it lives in the KyroSpades repo root and the
# Android project is at <repo>/android — which is the committed layout.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KS_DIR="${1:-$SCRIPT_DIR}"
ANDROID_DIR="${2:-$SCRIPT_DIR/android}"

# Resolve absolute paths
KS_DIR="$(cd "$KS_DIR" && pwd)"
ANDROID_DIR="$(cd "$ANDROID_DIR" && pwd)"

ASSETS="$ANDROID_DIR/app/src/main/assets"

echo "KyroSpades source : $KS_DIR"
echo "Android project   : $ANDROID_DIR"
echo "Assets target     : $ASSETS"
echo

# Sanity checks
[[ -d "$KS_DIR/resources" ]] || { echo "ERROR: $KS_DIR/resources not found"; exit 1; }

# bsresources.zip: same source the desktop CMake build uses
if [[ ! -f "$KS_DIR/bsresources.zip" ]]; then
    echo "bsresources.zip not found — downloading ..."
    curl -L --fail -o "$KS_DIR/bsresources.zip" http://aos.party/bsresources.zip
fi

# Start clean so re-runs are deterministic
rm -rf "$ASSETS"
mkdir -p "$ASSETS"

# 1. Base resources (config.ini, fonts/, png/, kv6/, icon.png …)
echo "[1/3] Copying resources/ ..."
cp -R "$KS_DIR/resources/." "$ASSETS/"
# icon.rc is a Windows resource script — not a game asset, drop it
rm -f "$ASSETS/icon.rc"

# 2. bsresources.zip — third-party kv6/png/wav. Extract on top.
echo "[2/3] Extracting bsresources.zip ..."
unzip -o -q "$KS_DIR/bsresources.zip" -d "$ASSETS"
# strip any stray .gitignore files that came along in the zip
find "$ASSETS" -name '.gitignore' -delete

# 3. Overrides win over everything
if [[ -d "$KS_DIR/resources.override" ]]; then
    echo "[3/3] Applying resources.override/ ..."
    cp -R "$KS_DIR/resources.override/." "$ASSETS/"
else
    echo "[3/3] No resources.override/ — skipping"
fi

echo
echo "Done. Asset tree summary:"
echo "  config.ini : $( [[ -f "$ASSETS/config.ini" ]] && echo present || echo MISSING )"
echo "  fonts/     : $(ls "$ASSETS/fonts" 2>/dev/null | wc -l | tr -d ' ') files"
echo "  png/       : $(find "$ASSETS/png" -type f 2>/dev/null | wc -l | tr -d ' ') files"
echo "  kv6/       : $(find "$ASSETS/kv6" -type f 2>/dev/null | wc -l | tr -d ' ') files"
echo "  wav/       : $(find "$ASSETS/wav" -type f 2>/dev/null | wc -l | tr -d ' ') files"
echo
echo "Total asset size: $(du -sh "$ASSETS" | cut -f1)"
echo
echo "Next: build the APK with"
echo "  cd \"$ANDROID_DIR\" && ./gradlew assembleDebug"
