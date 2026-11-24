# PS2 games on the Windows GPU in WSL 2

This optional x86-64 package runs the Windows PS2 games through GE-Proton, using Mesa Dozen and the GPU shared by Windows. It is intended for Ubuntu 24.04 in WSL 2 with WSLg. Native Linux and SteamOS use their own Vulkan drivers.

Extract the WSL archive beside `EVAC PC LABEL.exe`. Keep the files in the Linux filesystem and configure the game's data directory in the launcher, or pass `--data-dir`. From the extracted collection, run:

```sh
export PROTON="$HOME/.local/share/Steam/compatibilitytools.d/GE-Proton11-12/proton"
sh wsl-ps2/run-ps2.sh ibara
```

Set `PROTON` to your installed executable. The other game IDs are `espgaluda`, `daioujou-ps2`, and `rtype-final`. Launcher options such as `--profile` are passed through. The script creates a dedicated Proton prefix by default. Set `STEAM_COMPAT_DATA_PATH` to reuse an existing prefix, particularly if your game data uses a custom Wine drive mapping. The script uses X11. In a WSLg session, Xwayland supplies the display. If a window fails to open, check that an X11 application such as `xdpyinfo` can connect to `$DISPLAY`. The Vulkan loader and X11 runtime libraries must be installed in the distribution.

The script selects the included driver for this launch and enables its 8-bit storage support. The GS renderer compiles the shader variants encountered in play. The first use of a variant can briefly stall. Check `logs/<game>/run-*/stderr.log` for `Using Vulkan GPU` to see the selected adapter.

The RTX 4080 was tested in all four games through the first playable stage. R-Type Final was also played through the WSLg desktop window. The driver initializes discarded color attachments so interlaced video plays with the original deinterlacing.

## Rebuild the package

Use an x86-64 Ubuntu 24.04 build environment. Install Meson 1.5 or later, Ninja, GCC/G++, Git, patch, xz-utils, tar, pkg-config, glslang-tools, bison, flex, Python Mako, PyYAML, packaging, PLY and Jinja2. Install development headers for libdrm, zlib, zstd, expat, X11-XCB, XCB (DRI3, Present, RandR, SHM, XFixes), libxshmfence and Xrandr.

Download the [Mesa 26.3.1 source archive](https://archive.mesa3d.org/mesa-26.3.1.tar.xz) and run:

```sh
sh build.sh mesa-26.3.1.tar.xz evac-wsl-ps2-x86_64.tar.gz
```

The build checks the archive SHA-256, applies the included Dozen patch and checks the Microsoft DirectX-Headers revision before compiling. The package includes the driver, launcher script, build inputs and third-party notices.
