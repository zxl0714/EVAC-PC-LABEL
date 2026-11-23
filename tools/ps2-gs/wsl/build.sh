#!/bin/sh
# Build the private x86-64 WSL driver in a fresh tree from the pinned Mesa release.
set -eu
if [ "$#" -ne 2 ]; then
    echo 'Usage: sh build.sh /path/to/mesa-26.3.1.tar.xz /path/to/output.tar.gz' >&2
    exit 2
fi
archive=$(realpath "$1")
output=$(realpath -m "$2")
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
printf '%s  %s\n' 4e4e438eac7b840a9383988cbb7c08dad8041da544b9319fc26d319fa7348961 "$archive" | sha256sum -c -
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
tar -xf "$archive" -C "$tmp"
src="$tmp/mesa-26.3.1"
patch -d "$src" -p1 < "$here/dozen.patch"
dx="$src/subprojects/DirectX-Headers-1.0"
git clone -q --depth 1 -b v1.619.1 "${DXHEADERS_URL:-https://github.com/microsoft/DirectX-Headers.git}" "$dx"
test "$(git -C "$dx" rev-parse HEAD)" = 9e393d6d8a3b30dcc6f2806ef604ec16a27b0d7e
meson setup "$tmp/build" "$src" --buildtype=release --wrap-mode=nodownload --force-fallback-for=DirectX-Headers \
    -Dgallium-drivers= -Dvulkan-drivers=microsoft-experimental -Dplatforms=x11 \
    -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=disabled -Dshared-glapi=disabled \
    -Dgles1=disabled -Dgles2=disabled -Dopengl=false -Dshared-llvm=disabled
driver=src/microsoft/vulkan/libvulkan_dzn.so
ninja -C "$tmp/build" -j "${JOBS:-8}" "$driver"
dest="$tmp/wsl-ps2"
mkdir -p "$dest/licenses"
install -s -m 755 "$tmp/build/$driver" "$dest/libvulkan_dzn.so"
header=$(awk '/^#define VK_HEADER_VERSION / { print $3; exit }' "$src/include/vulkan/vulkan_core.h")
test -n "$header"
printf '{"file_format_version":"1.0.0","ICD":{"library_path":"./libvulkan_dzn.so","api_version":"1.2.%s"}}\n' "$header" > "$dest/dzn.json"
install -m 755 "$here/run-ps2.sh" "$dest/"
cp "$here/README.md" "$here/build.sh" "$here/dozen.patch" "$dest/"
cp -r "$src/licenses" "$dest/licenses/Mesa"
cp "$src/docs/license.rst" "$dest/licenses/Mesa/"
cp "$dx/LICENSE" "$dest/licenses/DirectX-Headers.txt"
LC_ALL=C grep -rhiE '(^|[[:space:]*#/])(Copyright|SPDX-FileCopyrightText:)' "$src/src" "$src/include" \
    --include='*.c' --include='*.cpp' --include='*.h' | LC_ALL=C sort -u > "$dest/licenses/Mesa-copyrights.txt"
tar -czf "$tmp/package.tar.gz" -C "$tmp" wsl-ps2
mkdir -p -- "$(dirname "$output")"
install -m 644 "$tmp/package.tar.gz" "$output"
