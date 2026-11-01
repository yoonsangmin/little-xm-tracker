# Reproducible, untouched upstream snapshot. A local checkout may be supplied
# explicitly when preparing an offline or cross-compilation build.
set(MILKYTRACKER_REVISION "e3ceb05105e8c5bdc907d6451245bf5a1926b1f9")
set(MILKYTRACKER_SOURCE_DIR "" CACHE PATH "Path to an unchanged MilkyTracker source tree")
if(NOT MILKYTRACKER_SOURCE_DIR)
    set(_milky_local "${CMAKE_SOURCE_DIR}/.deps/MilkyTracker-${MILKYTRACKER_REVISION}")
    if(EXISTS "${_milky_local}/src/milkyplay/PlayerSTD.cpp")
        set(MILKYTRACKER_SOURCE_DIR "${_milky_local}")
    else()
        include(FetchContent)
        FetchContent_Declare(milkytracker_source
            URL "https://codeload.github.com/milkytracker/MilkyTracker/zip/${MILKYTRACKER_REVISION}"
            URL_HASH SHA256=e25a72c6b0a4f656f71ca70c667e03ccb7097ff121889d1df360f51e57245325
            SOURCE_SUBDIR do-not-build-upstream-ui
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(milkytracker_source)
        set(MILKYTRACKER_SOURCE_DIR "${milkytracker_source_SOURCE_DIR}")
    endif()
endif()

# Import-only decoder snapshot, including the bundled untouched stb_vorbis.
set(MINIAUDIO_SOURCE_DIR "" CACHE PATH "Path to an unchanged miniaudio 0.11.23 source tree")
if(NOT MINIAUDIO_SOURCE_DIR)
    set(_miniaudio_local "${CMAKE_SOURCE_DIR}/.deps/miniaudio-0.11.23")
    if(EXISTS "${_miniaudio_local}/miniaudio.h" AND EXISTS "${_miniaudio_local}/extras/stb_vorbis.c")
        set(MINIAUDIO_SOURCE_DIR "${_miniaudio_local}")
    else()
        include(FetchContent)
        FetchContent_Declare(miniaudio_source
            URL "https://codeload.github.com/mackron/miniaudio/zip/refs/tags/0.11.23"
            URL_HASH SHA256=96d032b8e77061c9e2c07fd524141b67f20fe99f86ec63243a3fd06ea7f93793
            SOURCE_SUBDIR do-not-build-upstream-devices
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(miniaudio_source)
        set(MINIAUDIO_SOURCE_DIR "${miniaudio_source_SOURCE_DIR}")
    endif()
endif()

find_package(SDL2 2.0.12 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2>=2.0.12)
    add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
endif()
