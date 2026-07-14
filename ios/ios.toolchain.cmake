# ─────────────────────────────────────────────────────────────────────────────
# Minimal, console-only iOS toolchain for KyroSpades.
#
# Wraps CMake's built-in iOS support (CMAKE_SYSTEM_NAME=iOS, available since
# CMake 3.14) so we can cross-compile with Ninja / Unix Makefiles instead of
# generating an Xcode project. We never open the Xcode IDE — only the SDK and
# clang that ship inside Xcode.app are used, driven entirely from the terminal.
#
# Tunables (pass with -D... or as environment, defaults shown):
#   IOS_SYSROOT            iphoneos | iphonesimulator   (default: iphoneos)
#   IOS_ARCH              arm64 | x86_64                (default: arm64)
#   IOS_DEPLOYMENT_TARGET min iOS version               (default: 13.0)
# ─────────────────────────────────────────────────────────────────────────────

if(NOT DEFINED IOS_SYSROOT)
	set(IOS_SYSROOT "iphoneos")
endif()
if(NOT DEFINED IOS_ARCH)
	set(IOS_ARCH "arm64")
endif()
if(NOT DEFINED IOS_DEPLOYMENT_TARGET)
	set(IOS_DEPLOYMENT_TARGET "13.0")
endif()

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR ${IOS_ARCH})

set(CMAKE_OSX_SYSROOT            ${IOS_SYSROOT}            CACHE STRING "" FORCE)
set(CMAKE_OSX_ARCHITECTURES      ${IOS_ARCH}              CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET  ${IOS_DEPLOYMENT_TARGET} CACHE STRING "" FORCE)

# Prefer libraries over same-named frameworks. Without this, FindOpenAL would
# grab a macOS OpenAL.framework (or nothing) instead of our cross-built
# libopenal.a in the dependency prefix.
set(CMAKE_FIND_FRAMEWORK LAST)

# find_*() resolution: look in the dependency prefix (added to
# CMAKE_FIND_ROOT_PATH via CMAKE_PREFIX_PATH at configure time) for headers and
# libraries, but allow host paths for build tools (git, ninja, etc).
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# We assemble and sign the .app bundle ourselves in build_ios.sh, so we do NOT
# want CMake's Xcode-only bundle/codesign machinery. A plain executable is fine.
set(CMAKE_MACOSX_BUNDLE OFF)
