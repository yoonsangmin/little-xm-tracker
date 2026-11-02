#!/bin/sh
# Little XM Tracker: install beside xm-tracker/ in SHARE/roms/ports.
# Keep this folder name so existing songs, settings and recovery stay available.
set -eu
BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR="$BASE_DIR/xm-tracker"
DATA_DIR="$APP_DIR/userdata"
mkdir -p "$DATA_DIR"

# Keep the previous launch for troubleshooting without growing the log forever.
if [ -f "$DATA_DIR/launcher.log" ]; then
    mv -f "$DATA_DIR/launcher.log" "$DATA_DIR/launcher.previous.log"
fi
exec >"$DATA_DIR/launcher.log" 2>&1
printf 'Little XM Tracker launch: %s\n' "$(date -Iseconds)"
printf 'System: %s\n' "$(uname -smr)"

if [ ! -f "$APP_DIR/littlexm-tracker" ]; then
    echo "Missing xm-tracker/littlexm-tracker. Copy the complete Brick package."
    exit 1
fi
if [ ! -x "$APP_DIR/littlexm-tracker" ]; then
    chmod u+x "$APP_DIR/littlexm-tracker"
fi
if [ ! -f "$APP_DIR/assets/fonts/unifont-18.0.01.hex" ]; then
    echo "Missing assets/fonts/unifont-18.0.01.hex. Copy the complete Brick package."
    exit 1
fi

# Normal Ports launches inherit this already; also support manual shell launches.
# Keep the firmware's running audio server and its selected output device.
if [ -z "${XDG_RUNTIME_DIR:-}" ] && [ -r /etc/profile.d/xdg.sh ]; then
    . /etc/profile.d/xdg.sh
fi

# KNULLI's default SDL mapping uses Xbox face-button positions. This application
# names buttons after Brick's printed labels. The last mapping wins for Brick's
# GUID; keep any firmware mappings for other controllers. Nothing is installed
# into the firmware or the user's persistent controller settings.
MAPPING_FILE="$APP_DIR/gamecontrollerdb.txt"
# The app creates its editable copy on first launch. Use the packaged default
# for this first process only; an existing empty file or symlink always wins.
if [ ! -e "$MAPPING_FILE" ] && [ ! -L "$MAPPING_FILE" ]; then
    MAPPING_FILE="$APP_DIR/assets/gamecontrollerdb.txt"
fi
if [ -r "$MAPPING_FILE" ]; then
    BRICK_MAPPING=$(sed '/^[[:space:]]*#/d; /^[[:space:]]*$/d' "$MAPPING_FILE" | tr -d '\r')
    if [ -n "$BRICK_MAPPING" ]; then
        if [ -n "${SDL_GAMECONTROLLERCONFIG:-}" ]; then
            SDL_GAMECONTROLLERCONFIG="$SDL_GAMECONTROLLERCONFIG
$BRICK_MAPPING"
        else
            SDL_GAMECONTROLLERCONFIG="$BRICK_MAPPING"
        fi
        export SDL_GAMECONTROLLERCONFIG
    fi
fi

# Use KNULLI's patched SDL2 and its video/audio auto-detection. Do not bundle a
# generic SDL2 or force ALSA, which could bypass the firmware's audio routing.
cd "$APP_DIR"
exec ./littlexm-tracker --fullscreen --data-dir "$DATA_DIR" "$@"
