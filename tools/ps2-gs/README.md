# PS2 GPU bring-up

Build the standalone GS device with the collection toolchain:

```powershell
./scripts/Build-Ps2Gs.ps1
./tools/apps/cmake/bin/ctest.exe --test-dir out/ps2-gs --output-on-failure
```

The target requires a hardware Vulkan device supporting the pinned paraLLEl-GS
feature set. It uses authored commands and pixels; no ROM, BIOS or game window
is needed. Sources restore from `tools/ps2-source.lock.json` into `.deps/ps2`.
An independently verified build view is created under `out/ps2-gs`.

The `ps2_gs_device` library owns graphics state through a small PIMPL interface.
It preserves the three raw GIF paths and enforces one command owner. The GPU
contract covers fragmented packets, drawing, explicit readback and API guards.
Readback is a test operation; game presentation must remain on the GPU.

Game command routing and direct window presentation are the next integration
steps. See [graphics architecture](../../docs/PS2_GRAPHICS_PLAN.md).
