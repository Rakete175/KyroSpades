# KyroSpades — iOS port (Phase 1)

This folder holds the iOS-specific build assets. The build is **console-only**
(no Xcode IDE) and is structured so a future GitHub Actions `macos` job can drive
it unchanged.

## Layout

```
build_ios.sh              (repo root) orchestrator — parallels build_android.sh
ios/
  ios.toolchain.cmake     CMake toolchain wrapping native iOS support (Ninja/Make)
  Info.plist              app bundle metadata (landscape, ATS, bundle id)
  PHASE1_patches.diff     review diff of all engine/CMake edits
  README.md               this file
```

Engine/build edits (also in PHASE1_patches.diff) live in their normal places:
`CMakeLists.txt`, `src/CMakeLists.txt`, `src/common.h`, `src/main.c`, `src/hud.c`.

## Build (on a Mac, terminal only)

```sh
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer   # once
brew install cmake ninja git                                      # once
./build_ios.sh                 # device (arm64/iphoneos)
IOS_SYSROOT=iphonesimulator ./build_ios.sh   # simulator
```

Output: `build-ios/KyroSpades.app` (UNSIGNED). Signing + install is Phase 2.

## What Phase 1 changes

- iOS GL ES headers + `OS_IOS` macro + conditional `SDL_MAIN_HANDLED` so SDL
  owns `main()` and boots the UIKit run loop (`src/common.h`).
- Sandbox storage: stage read-only bundle resources into the writable prefs dir
  once, then `chdir` there so every existing relative path keeps working
  (`src/main.c`). Verified against all loaders (texture/font/dir-list/config).
- CMake: force SDL+GLES+touch on iOS, skip the Homebrew probe, link OpenGLES +
  OpenAL CoreAudio + SDL2main + UIKit frameworks.
- Skip the defunct aos.party news fetch on iOS (`src/hud.c`).

## HTTP note

The client uses a **plaintext-only** HTTP library (`src/http.h`). It cannot do
https — it rejects any non-`http://` URL outright. The server list therefore
stays on `http://` and relies on the `NSAllowsArbitraryLoads` exception in
`Info.plist`. Moving to https would require a real TLS path (libcurl, or an
NSURLSession shim on iOS) — a separate task, not part of Phase 1.

## Known first-build risk spots

1. **ES1/ES2 header co-inclusion** (`common.h`): Apple's `<OpenGLES/ES1/gl.h>`
   and `<OpenGLES/ES2/gl2.h>` may clash on overlapping constants. If so, keep
   only the ES2 includes — the ES1 *symbols* still link from OpenGLES.framework.
2. **SDL2main dropped** → black screen, no crash. Force-keep it:
   `target_link_libraries(client "-Wl,-force_load" SDL2::SDL2main)`.
3. openal-soft CoreAudio flag names drift between versions.
4. `<SDL2/SDL.h>` resolution depends on `cmake --install` laying out
   `include/SDL2/` (it does for SDL 2.30.x).

## Before signing

Change the bundle identifier in `Info.plist` (`win.penguins.kyrospades`) to
something unique to your Apple ID, or free-tier signing will reject it.
