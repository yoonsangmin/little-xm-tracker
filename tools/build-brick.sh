#!/usr/bin/env bash
# Run under Linux. Reuses an ARM64 SDK without changing its contents.
set -euo pipefail
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir="$project_dir/build-brick"

: "${KNULLI_SDK:?Set KNULLI_SDK to your ARM64 SDK directory}"
KNULLI_TARGET=${KNULLI_TARGET:-aarch64-buildroot-linux-gnu}
KNULLI_SYSROOT=${KNULLI_SYSROOT:-$KNULLI_SDK/$KNULLI_TARGET/sysroot}
compiler="$KNULLI_SDK/bin/$KNULLI_TARGET-g++"
readelf_tool="$KNULLI_SDK/bin/$KNULLI_TARGET-readelf"

# Optional user-mode emulator. No emulation files enter the device package.
qemu=${QEMU_AARCH64:-$project_dir/.deps/qemu-user-static/root/usr/bin/qemu-aarch64-static}
emulator=""
if [[ -x "$qemu" ]]; then
    cxx_lib_dir=$(dirname -- "$("$compiler" -print-file-name=libstdc++.so)")
    test_sdl=${QEMU_LIBRARY_PATH:-$project_dir/.deps/sdl2-arm64-test/stage/lib}
    test_libs="$KNULLI_SYSROOT/lib:$cxx_lib_dir:$KNULLI_SYSROOT/usr/lib"
    if [[ -d "$test_sdl" ]]; then test_libs="$test_sdl:$test_libs"; fi
    emulator="$qemu;-L;$KNULLI_SYSROOT;-E;LD_LIBRARY_PATH=$test_libs"
fi

cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/knulli-aarch64.cmake" \
    -DKNULLI_SDK="$KNULLI_SDK" -DKNULLI_TARGET="$KNULLI_TARGET" \
    -DKNULLI_SYSROOT="$KNULLI_SYSROOT" \
    -DCMAKE_CROSSCOMPILING_EMULATOR="$emulator" \
    -DTRACKER_BUILD_APP=ON -DTRACKER_BUILD_TESTS=ON
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-4}"

test_status='ARM64 runtime tests not run (QEMU_AARCH64 not available).'
if [[ ${SKIP_TESTS:-0} == 1 ]]; then
    test_status='ARM64 Release build completed. Runtime tests deliberately deferred for this build.'
elif [[ -n "$emulator" ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
    test_status='All ARM64 CTest checks passed under QEMU with SDL dummy drivers (test-only SDL runtime; device drivers untested).'
fi

{
    printf 'Little XM Tracker / TrimUI Brick / KNULLI ARM64\nBuilt: %s\n' "$(date -u +%FT%TZ)"
    printf '%s\n' "$test_status"
    printf 'Physical Brick screen, buttons, audio latency and suspend remain untested.\n'
    "$compiler" --version | head -1
    "$readelf_tool" -h "$build_dir/littlexm-tracker" | grep -E 'Class:|Machine:'
    "$readelf_tool" -d "$build_dir/littlexm-tracker" | grep -E 'NEEDED|RPATH|RUNPATH'
    "$readelf_tool" --version-info "$build_dir/littlexm-tracker" | grep 'Name:'
} > "$build_dir/BUILD-STATUS.txt"

python3 "$project_dir/tools/package-brick.py" "$build_dir"
