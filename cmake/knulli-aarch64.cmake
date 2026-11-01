# Supply the SDK exported by KNULLI, or an older compatible A133 SDK.
# SDL2 remains dynamically linked to the device firmware's video/audio backend.
# Usage: cmake -S . -B build-brick -DCMAKE_TOOLCHAIN_FILE=cmake/knulli-aarch64.cmake
#        -DKNULLI_SDK=/path/to/sdk -DKNULLI_TARGET=aarch64-buildroot-linux-gnu
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES KNULLI_SDK KNULLI_TARGET KNULLI_SYSROOT)
set(KNULLI_SDK "" CACHE PATH "KNULLI Buildroot host SDK directory")
set(KNULLI_TARGET "aarch64-buildroot-linux-gnu" CACHE STRING "SDK compiler triplet")
if(NOT KNULLI_SDK)
    message(FATAL_ERROR "Set KNULLI_SDK to an ARM64 SDK with target SDL2 headers/libraries. A generic host SDL2 is not sufficient.")
endif()
set(CMAKE_C_COMPILER "${KNULLI_SDK}/bin/${KNULLI_TARGET}-gcc")
set(CMAKE_CXX_COMPILER "${KNULLI_SDK}/bin/${KNULLI_TARGET}-g++")
set(KNULLI_SYSROOT "${KNULLI_SDK}/${KNULLI_TARGET}/sysroot" CACHE PATH "Target sysroot (some SDKs use <triplet>/libc)")
set(CMAKE_SYSROOT "${KNULLI_SYSROOT}")
# Prefer the compiler's matching glibc for indirect SDL2 dependencies. Some
# A133 SDKs also carry older runtime copies under usr/lib.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-rpath-link,${CMAKE_SYSROOT}/lib")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
