#!/usr/bin/env bash
# Optional offline cache for the pinned dependencies; SDL2 comes from the system.
set -euo pipefail
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
deps_dir="$project_dir/.deps"
mkdir -p "$deps_dir"

fetch_archive() {
    local name=$1 url=$2 digest=$3 directory=$4 archive="$deps_dir/$1"
    if [[ ! -f "$archive" ]]; then
        curl --fail --location --retry 2 --max-time 180 "$url" -o "$archive.partial"
        printf '%s  %s\n' "$digest" "$archive.partial" | sha256sum --check --status
        mv -- "$archive.partial" "$archive"
    fi
    printf '%s  %s\n' "$digest" "$archive" | sha256sum --check
    if [[ ! -d "$deps_dir/$directory" ]]; then
        unzip -q "$archive" -d "$deps_dir"
    fi
}

fetch_archive milkytracker-source.zip \
    https://codeload.github.com/milkytracker/MilkyTracker/zip/e3ceb05105e8c5bdc907d6451245bf5a1926b1f9 \
    e25a72c6b0a4f656f71ca70c667e03ccb7097ff121889d1df360f51e57245325 \
    MilkyTracker-e3ceb05105e8c5bdc907d6451245bf5a1926b1f9
fetch_archive miniaudio-0.11.23.zip \
    https://codeload.github.com/mackron/miniaudio/zip/refs/tags/0.11.23 \
    96d032b8e77061c9e2c07fd524141b67f20fe99f86ec63243a3fd06ea7f93793 \
    miniaudio-0.11.23
printf 'Pinned MilkyPlay and import decoder sources are ready.\n'
