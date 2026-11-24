#!/bin/sh
set -eu
if [ "$#" -lt 1 ]; then
    echo 'Usage: PROTON=/path/to/proton sh wsl-ps2/run-ps2.sh GAME [launcher options]' >&2
    exit 2
fi
game=$1
shift
case "$game" in
    espgaluda|ibara|daioujou-ps2|rtype-final) ;;
    *) echo "Unknown PS2 game: $game" >&2; exit 2 ;;
esac

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname -- "$here")
if [ ! -f "$root/EVAC PC LABEL.exe" ]; then
    echo 'Extract the wsl-ps2 directory beside EVAC PC LABEL.exe.' >&2
    exit 1
fi
if [ ! -c /dev/dxg ] || [ ! -r /usr/lib/wsl/lib/libd3d12.so ]; then
    echo 'The Windows GPU and its D3D12 runtime must be visible in WSL 2.' >&2
    exit 1
fi
if [ ! -r "$here/dzn.json" ] || [ ! -r "$here/libvulkan_dzn.so" ]; then
    echo 'The private Dozen driver is missing. Extract the complete WSL package.' >&2
    exit 1
fi
if [ ! -x "${PROTON:-}" ]; then
    echo 'Set PROTON to the absolute path of the GE-Proton proton executable.' >&2
    exit 1
fi
PROTON=$(realpath -- "$PROTON")
if [ -z "${DISPLAY:-}" ]; then
    echo 'An X11 display is required. Check that WSLg or your X server is running.' >&2
    exit 1
fi
# The private ICD is built with X11 WSI. Let Proton use Xwayland on WSLg.
unset WAYLAND_DISPLAY
: "${STEAM_COMPAT_DATA_PATH:=${XDG_DATA_HOME:-$HOME/.local/share}/evac/wsl-ps2}"
: "${STEAM_COMPAT_CLIENT_INSTALL_PATH:=$HOME/.local/share/Steam}"
mkdir -p -- "$STEAM_COMPAT_DATA_PATH"
export STEAM_COMPAT_DATA_PATH STEAM_COMPAT_CLIENT_INSTALL_PATH
export LD_LIBRARY_PATH="/usr/lib/wsl/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export VK_DRIVER_FILES="$here/dzn.json" VK_ICD_FILENAMES="$here/dzn.json"
export dzn_enable_8bit_loads_stores=true PGS_SKIP_COMPILATION_TASKS=1
cd -- "$root"
exec "$PROTON" run "$root/EVAC PC LABEL.exe" --game "$game" "$@"
