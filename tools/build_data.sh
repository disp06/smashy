#!/bin/sh
# Rebuild the Vita data folder from your own copy of the game.
# usage: tools/build_data.sh path/to/game_armeabi-v7a.apk [workdir]
set -e
APK_FILE="$1"; WORK="${2:-work}"
[ -f "$APK_FILE" ] || { echo "usage: $0 game_armeabi-v7a.apk [workdir]"; exit 1; }
TOOLS="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$WORK/apk" "$WORK/dump" "$WORK/data"
unzip -q -o "$APK_FILE" -d "$WORK/apk"
export SMASHY_APK_DIR="$WORK/apk" SMASHY_WORK="$WORK"
python3 "$TOOLS/dumpmb.py"
python3 "$TOOLS/tiles.py"
python3 "$TOOLS/export.py" "$WORK/data"
python3 "$TOOLS/export_ui.py"
python3 "$TOOLS/pack.py"
python3 "$TOOLS/gen_gamedata.py"
echo "Done: copy $WORK/data/*.pak to ux0:data/smashy/ on the Vita"
