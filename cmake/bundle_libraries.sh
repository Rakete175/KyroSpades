#!/usr/bin/env bash
# bundle_libraries.sh — copy FFmpeg (and other non-system) shared libraries
# next to the executable so the build is portable and self-contained.
set -u

EXE="$1"
if [ -z "$EXE" ]; then
    echo "bundle_libraries: ERROR: no executable path given" >&2
    exit 1
fi

if [ ! -f "$EXE" ]; then
    echo "bundle_libraries: WARNING: executable not found at $EXE — skipping" >&2
    exit 0
fi

EXE_DIR="$(cd "$(dirname "$EXE")" && pwd)"
LIB_DIR="$EXE_DIR/lib"
mkdir -p "$LIB_DIR"

FFMPEG_LIBS="avcodec avformat avutil swresample swscale"
EXTRA_LIBS="openal pulse pulse-simple"

OS="$(uname -s)"
case "$OS" in
    Linux*)
        echo "=== Bundle (Linux): copying FFmpeg + audio libs to $LIB_DIR ==="
        if ! command -v ldd >/dev/null 2>&1; then
            echo "bundle_libraries: WARNING: ldd not found — skipping" >&2
            exit 0
        fi
        LDD_OUT="$(ldd "$EXE" 2>/dev/null)"
        for lib in $FFMPEG_LIBS $EXTRA_LIBS; do
            path=$(echo "$LDD_OUT" | grep "lib${lib}\." | awk '{print $3}' | head -1)
            if [ -n "$path" ] && [ -f "$path" ]; then
                dir=$(dirname "$path")
                cp -aL "$dir"/lib${lib}.so* "$LIB_DIR/" 2>/dev/null && echo "  copied: lib${lib}.so*" || true
            fi
        done
        if command -v patchelf >/dev/null 2>&1; then
            patchelf --set-rpath '$ORIGIN/lib' "$EXE" 2>/dev/null || true
        fi
        echo "=== Bundle (Linux): done ==="
        ;;
    Darwin*)
        echo "=== Bundle (macOS): copying FFmpeg + audio libs to $LIB_DIR ==="
        if ! command -v otool >/dev/null 2>&1; then
            echo "bundle_libraries: WARNING: otool not found — skipping" >&2
            exit 0
        fi
        OTOOL_OUT="$(otool -L "$EXE" 2>/dev/null)"
        for lib in $FFMPEG_LIBS $EXTRA_LIBS; do
            path=$(echo "$OTOOL_OUT" | grep "lib${lib}" | awk '{print $1}' | head -1)
            if [ -n "$path" ] && [ -f "$path" ]; then
                cp -aL "$path" "$LIB_DIR/" 2>/dev/null && echo "  copied: $(basename "$path")" || true
            fi
        done
        if command -v install_name_tool >/dev/null 2>&1; then
            for f in "$LIB_DIR"/*.dylib; do
                [ ! -f "$f" ] && continue
                name=$(basename "$f")
                install_name_tool -id "@rpath/$name" "$f" 2>/dev/null || true
                deps=$(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}')
                for dep in $deps; do
                    case "$dep" in
                        /usr/lib/*|/System/*|@rpath/*|@loader_path/*|@executable_path/*) ;;
                        *)
                            depname=$(basename "$dep")
                            if [ -f "$LIB_DIR/$depname" ]; then
                                install_name_tool -change "$dep" "@rpath/$depname" "$f" 2>/dev/null || true
                            fi
                            ;;
                    esac
                done
            done
        fi
        echo "=== Bundle (macOS): done ==="
        ;;
    MINGW*|MSYS*|CYGWIN*)
        echo "=== Bundle (Windows): copying FFmpeg + audio DLLs to $EXE_DIR ==="
        if command -v ldd >/dev/null 2>&1; then
            LDD_OUT="$(ldd "$EXE" 2>/dev/null)"
            for lib in $FFMPEG_LIBS $EXTRA_LIBS; do
                path=$(echo "$LDD_OUT" | grep -i "lib${lib}" | awk '{print $3}' | head -1)
                if [ -n "$path" ] && [ -f "$path" ]; then
                    cp -n "$path" "$EXE_DIR/" 2>/dev/null && echo "  copied (ldd): $(basename "$path")" || true
                fi
            done
        fi
        for lib in $FFMPEG_LIBS $EXTRA_LIBS; do
            for dir in /ucrt64/bin /mingw64/bin /usr/bin "C:/msys64/ucrt64/bin" "C:/msys64/mingw64/bin"; do
                if [ -d "$dir" ]; then
                    for f in "$dir"/lib${lib}*.dll "$dir"/av${lib}*.dll "$dir"/OpenAL32.dll; do
                        if [ -f "$f" ]; then
                            cp -n "$f" "$EXE_DIR/" 2>/dev/null && echo "  copied (glob): $(basename "$f")" || true
                        fi
                    done
                fi
            done
        done
        if command -v ldd >/dev/null 2>&1; then
            for dll in "$EXE_DIR"/libav*.dll "$EXE_DIR"/libsw*.dll; do
                [ ! -f "$dll" ] && continue
                for dep in $(ldd "$dll" 2>/dev/null | grep '=> /' | awk '{print $3}'); do
                    case "$(basename "$dep")" in
                        KERNEL32.dll|msvcrt.dll|winmm.dll|ole32.dll|oleaut32.dll|ws2_32.dll|advapi32.dll|user32.dll|gdi32.dll|shell32.dll|msvcp*.dll|libgcc_s*.dll|libstdc*.dll|libwinpthread*.dll|m*.dll|api-ms-*) ;;
                        *)
                            [ -f "$dep" ] && cp -n "$dep" "$EXE_DIR/" 2>/dev/null && echo "  copied (dep): $(basename "$dep")" || true
                            ;;
                    esac
                done
            done
        fi
        echo "=== Bundle (Windows): done ==="
        ;;
    *)
        echo "bundle_libraries: WARNING: unrecognized OS '$OS' — skipping" >&2
        exit 0
        ;;
esac

echo "--- Bundled libraries ---"
case "$OS" in
    Linux*|Darwin*)
        ls -1 "$LIB_DIR/" 2>/dev/null | head -30
        echo "Total: $(ls -1 "$LIB_DIR/" 2>/dev/null | wc -l) file(s)"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        ls -1 "$EXE_DIR"/libav*.dll "$EXE_DIR"/libsw*.dll 2>/dev/null | head -30
        echo "Total: $(ls -1 "$EXE_DIR"/libav*.dll "$EXE_DIR"/libsw*.dll 2>/dev/null | wc -l) FFmpeg DLL(s)"
        ;;
esac
