#!/usr/bin/env bash
# TEST ONLY: provide SDL dummy video/audio to QEMU. Never ship this library.
# The device build still links the SDK's SDL2 and uses firmware backends.
set -euo pipefail
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work_dir="$project_dir/.deps/sdl2-arm64-test"
version=2.32.8
source_url="https://www.libsdl.org/release/SDL2-$version.tar.gz"
source_sha256=0ca83e9c9b31e18288c7ec811108e58bac1f1bb5ec6577ad386830eac51c787e
archive="$work_dir/SDL2-$version.tar.gz"
source_dir="$work_dir/SDL2-$version"
build_dir="$work_dir/build"
stage_dir="$work_dir/stage"

: "${KNULLI_SDK:?Set KNULLI_SDK to your ARM64 SDK directory}"
KNULLI_TARGET=${KNULLI_TARGET:-aarch64-buildroot-linux-gnu}
KNULLI_SYSROOT=${KNULLI_SYSROOT:-$KNULLI_SDK/$KNULLI_TARGET/sysroot}

mkdir -p "$work_dir"
if [[ ! -f "$archive" ]]; then
    curl --fail --location --retry 2 --max-time 180 "$source_url" -o "$archive.partial"
    printf '%s  %s\n' "$source_sha256" "$archive.partial" | sha256sum --check --status
    mv -- "$archive.partial" "$archive"
fi
printf '%s  %s\n' "$source_sha256" "$archive" | sha256sum --check
if [[ ! -f "$source_dir/CMakeLists.txt" ]]; then
    tar -xf "$archive" -C "$work_dir"
fi

cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/knulli-aarch64.cmake" \
    -DKNULLI_SDK="$KNULLI_SDK" -DKNULLI_TARGET="$KNULLI_TARGET" \
    -DKNULLI_SYSROOT="$KNULLI_SYSROOT" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L$KNULLI_SYSROOT/lib -Wl,-rpath-link,$KNULLI_SYSROOT/lib" \
    -DCMAKE_INSTALL_PREFIX="$stage_dir" -DCMAKE_INSTALL_LIBDIR=lib \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_TESTS=OFF \
    -DSDL2_DISABLE_SDL2MAIN=ON -DSDL2_DISABLE_UNINSTALL=ON \
    -DSDL_AUDIO=ON -DSDL_VIDEO=ON -DSDL_RENDER=ON \
    -DSDL_DUMMYVIDEO=ON -DSDL_DUMMYAUDIO=ON -DSDL_DISKAUDIO=OFF \
    -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF \
    -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
    -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF -DSDL_VIVANTE=OFF -DSDL_OFFSCREEN=OFF \
    -DSDL_ALSA=OFF -DSDL_OSS=OFF -DSDL_JACK=OFF -DSDL_ESD=OFF \
    -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_ARTS=OFF \
    -DSDL_NAS=OFF -DSDL_SNDIO=OFF -DSDL_FUSIONSOUND=OFF \
    -DSDL_LIBSAMPLERATE=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
    -DSDL_HIDAPI=OFF -DSDL_LIBUDEV=OFF -DSDL_CCACHE=OFF \
    -DSDL_RPATH=OFF -DSDL_PTHREADS=ON
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-4}"
cmake --install "$build_dir"
grep -q '^#define SDL_AUDIO_DRIVER_DUMMY 1$' "$stage_dir/include/SDL2/SDL_config.h"
grep -q '^#define SDL_VIDEO_DRIVER_DUMMY 1$' "$stage_dir/include/SDL2/SDL_config.h"

{
    printf 'SDL2 %s: TEST-ONLY ARM64 runtime for QEMU dummy drivers\n' "$version"
    printf 'Source: %s\nSHA256: %s\n' "$source_url" "$source_sha256"
    printf 'Prepared: %s\n' "$(date -u +%FT%TZ)"
    printf 'SDK: %s\nSysroot: %s\n' "$KNULLI_SDK" "$KNULLI_SYSROOT"
    "$KNULLI_SDK/bin/$KNULLI_TARGET-readelf" -h "$stage_dir/lib/libSDL2-2.0.so.0" | grep -E 'Class:|Machine:'
    "$KNULLI_SDK/bin/$KNULLI_TARGET-readelf" -d "$stage_dir/lib/libSDL2-2.0.so.0" | grep -E 'NEEDED|SONAME|RPATH|RUNPATH'
    grep -E '^#define SDL_(AUDIO|VIDEO)_DRIVER_DUMMY 1$' "$stage_dir/include/SDL2/SDL_config.h"
    printf 'Prepend %s to the QEMU guest LD_LIBRARY_PATH for tests only.\n' "$stage_dir/lib"
    printf 'Do not copy this library into the device distribution or SDK.\n'
} > "$work_dir/BUILD-STATUS.txt"
printf '\nTest-only SDL2 runtime: %s\n' "$stage_dir/lib"
