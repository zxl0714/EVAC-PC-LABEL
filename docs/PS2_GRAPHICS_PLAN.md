# PS2 graphics architecture

Status: raw-command, GPU pixel/transfer and bounded-queue contracts passed on
RTX 4080. Direct GPU window output, resize and two-thread submission have also
passed an authored workload. Game integration remains pending. This plan takes
precedence over tuning the initial CPU rasterizer.

## Renderer choice

Use the standalone **paraLLEl-GS Vulkan compute renderer** as the primary integration
candidate. Keep the current CPU renderer for small mechanism checks and comparison.
The GPU path must pass the representative game workload before becoming default.

| Candidate | Useful experience | Integration decision |
| --- | --- | --- |
| [paraLLEl-GS](https://github.com/Arntzen-software/parallel-gs/tree/cc6184af7e0c03da603045ca371ffa5dae9b0655) | Independent GS implementation, GIF/register input, texture/VRAM hazards, compute rasterization, scanout and dump replay | Selected for standalone bring-up |
| [PCSX2](https://github.com/PCSX2/pcsx2) | Broad GS behavior coverage, hardware/software comparison, game-specific test scenes and GS dumps | Behavioral and regression reference; avoid importing its whole runtime |
| [Play!](https://github.com/jpd002/Play-/tree/1f27232772e484bfb79364b751b84f41f4c87481/Source/gs) | Existing GL/Vulkan GS designs and explicit component ownership | Secondary implementation reference |
| [DBZIW recomp integration](https://github.com/TheSlackbringer/dbziw-recomp/tree/1e8ae9c6d8a8df677ddfad55a0b52dff6d23f8d1/external/PS2Recomp/ps2xRuntime/src/lib/gs) | Demonstrates this renderer alongside a statically recompiled EE runtime | Reference adapter only; review its decoded-state translation and CPU scanout readback separately |

paraLLEl-GS's stated goal is software-renderer-style accuracy on a GPU. It does
not claim bit-exact real-hardware interpolation, and documents unresolved AA1
details. Correctness still needs game and small authored tests.

## Command and memory ownership

```text
native EE game code
  -> DMA / VIF / VU XGKICK
  -> ordered GIF paths 1 / 2 / 3
  -> graphics command boundary + optional capture
  -> paraLLEl-GS register/transfer processing
  -> GPU-owned 4 MiB GS VRAM + tracked texture/CLUT dependencies
  -> GPU scanout image
  -> Vulkan presentation
```

- Preserve original GIF path identity, packet continuation and command order.
  The adapted arbiter now passes path identity to `GSInterface::gif_transfer`.
  VIF IMAGE continuations retain the original payload; native image/register
  shortcuts enter the same GPU state owner. These have authored regressions.
- Include direct SDK register/image-upload shortcuts, not just DMA packets.
  Each command goes through one authoritative state owner. The CPU decoder and
  GPU decoder must not both draw the same packet or disagree about CLUT state.
- Prefer original GIF/register input over reconstructing a stream from floating
  point triangles. Retain ADC/kick suppression, packed Q/ST/UV semantics,
  transfers, texture feedback and both GS contexts.
- The backend owns logical GS VRAM. CPU access uses explicit mapped ranges and
  dependency tracking. A CPU mirror is not silently authoritative after GPU work.
- Preserve host-to-local, local-to-local and local-to-host transfers, TEXFLUSH,
  FINISH/SIGNAL/LABEL, masks, blending and overlapping render/texture regions.
  Actual readback requests may wait for the GPU; ordinary drawing does not.

The existing `GSRasterBackend::Submit(GSPrimitiveBatch)` is useful for CPU tests.
It is not the final transport contract for the GPU. Introduce the command-level
boundary without changing generated game functions or game-specific addresses.

## Threading and presentation

- One owner serializes GS state and Vulkan command submission. Worker threads,
  if used internally by the renderer, do not independently mutate game state.
- Transfer command payloads into bounded owned storage before the producer can
  reuse guest memory. Apply backpressure at a documented boundary; no unbounded
  packet queue and no pointer into a recycled DMA buffer.
- `GsCommandQueue` currently reserves at most 8 MiB and 2048 commands, including
  the command in execution. It copies payloads only after capacity is available.
  FIFO/snapshot/reset requests return through ordered replies. A frame request
  waits for consumer submission; errors and shutdown release every waiter.
- Separate VBlank/game time, GS completion and host presentation. Preserve the
  source field/frame cadence, parity, CRT aspect and display circuits.
- Normal presentation keeps scanout on the GPU. Avoid the current route of
  whole-VRAM snapshot -> CPU RGBA conversion -> OpenGL texture upload.
- `GsFrame` carries complete privileged timing/circuit registers and actual FIELD
  state, copied on the EE producer. The GPU owner never reads live guest display
  registers. This is independent of the CPU renderer's presentation hints.
- The GS public header forward-declares the command backend; its destructor is
  defined out of line. Changes to queue/renderer/command implementation do not
  force a rebuild of all generated game code.
- Vulkan surface creation and presentation belong to the window/device owner.
  Reuse the collection's window/input conventions; do not add a second frontend.
- Start with one outstanding game frame. Configure internal resource contexts
  independently; more allocator contexts do not justify queuing extra frames.
- The consumer's begin-frame hook waits for the previous submission fence before
  executing the next field's GS commands. Waiting only at `Present()` would let
  the next field's rendering enter the GPU before this limit took effect.
- Select present mode from supported capabilities and use an explicit fallback.
  Measure input sample -> game draw -> GPU -> present with the existing reporting
  convention. Immediate presentation alone is not a latency measurement.
- Reserve `device.wait_idle()` for shutdown/device rebuild and explicit test
  readback. It must not become a per-draw or per-frame synchronization shortcut.

## Dependencies and compiler

`tools/ps2-source.lock.json` now pins paraLLEl-GS, its Granite revision, volk and
Vulkan-Headers. Restore independent checkouts through `ps2_sources.py`, then
`ps2_gs_sources.py` assembles a build-local view matching the original submodule
pins. Original dependency trees are not patched or linked to another project.

`scripts/Build-Ps2Gs.ps1` uses the collection's LLVM, CMake, Ninja, CRT and Windows
SDK setup. The standalone target builds only the GS and supporting Vulkan/math
libraries. Runtime shader compilation, another frontend/audio stack, native CPU
tuning and fast math are disabled. The renderer's checked-in shader package is
used first; shader changes require a separately pinned generation workflow.

`backends/ps2/gs_device.{h,cpp}` is the collection-owned PIMPL boundary. It owns
the Vulkan context/device/GS in dependency order, enforces one command thread,
preserves raw GIF paths 1/2/3, and exposes explicit register/VRAM/FIFO operations.
It imports no window or input library. The GPU contract uses this actual module
and checks interleaved partial GIF tags, exact pixels and rejected invalid calls.

The current renderer requires specific Vulkan features: descriptor indexing,
timeline semaphores, buffer device addresses, 8/16-bit storage, shaderInt16,
scalar layout, subgroup operations/size control and at least 32 KiB of compute
shared memory. Probe the pinned implementation's actual predicate, not just the
driver's Vulkan version. Record missing capabilities and selected GPU. A software
Vulkan device does not qualify as GPU acceptance.

paraLLEl-GS is LGPL-3.0-or-later; retain its license and corresponding sources
alongside the existing dependency notices. Granite and its dependencies retain
their own notices. No additional source attachment is added to player packages.

## Acceptance order

1. **Standalone GPU contract:** build with the shared toolchain, create a real
   Vulkan device, submit authored GS commands and check exact pixels on readback.
   No ROM, BIOS, game process or window is needed.
2. **Command boundary:** cover all three GIF paths, packet fragments, direct
   register/image shortcuts, reset and transfer ordering with small tests.
3. **Offline GS workloads:** capture a bounded stream with initial VRAM, registers,
   field state and ordered commands. Replay without EE/IOP/input. Keep game-derived
   captures local; commit authored fixtures only. Run the same stream through CPU
   and GPU backends and compare pixels/state plus CPU/GPU timings.
4. **Direct presentation:** bind the GPU scanout to the window, verify resize,
   fullscreen, parity/aspect, bounded frames and normal shutdown. Keep debug
   readback off this normal path.
5. **Games:** ESPGaluda menu, character selection and stage 1 with effects;
   ARCADE/ARRANGE representatives; then Ibara. Check continuous game cadence and
   audio queue stability together. Record remaining visual differences explicitly.

Do not optimize isolated triangle loops while this integration remains pending.
Renderer bring-up, correct command transport and presentation are separate gates;
passing one does not establish the others.

## Integration gates still open

- **Completion and interrupts:** SIGNAL/FINISH/LABEL callbacks from paraLLEl-GS
  report command processing. Returning true flushes commands but does not wait
  for their GPU completion. Define an ordered fence/event handoff to the EE CSR
  state, preserve masks and reset generations, and cover polling without a frame
  being submitted. Never set FINISH merely because a packet reached the queue.
- **Offline replay:** capture from a known reset state, including initial VRAM,
  privileged timing, all ordered paths/registers/images and field boundaries.
  A mid-packet or arbitrary mid-game VRAM-only snapshot is incomplete: draw
  registers, GIF continuations and transfer state also matter. Keep captures
  bounded and private; replay without EE/IOP to isolate GS correctness and cost.
- **Production WSI:** direct blit checks the source/destination format features
  and swapchain transfer-destination support. A GPU sampled-image path uses the
  original renderer's checked-in shaders when direct blit is unavailable; its
  forced path has passed the authored window test. Verify fullscreen/minimize,
  device loss and unsupported capabilities before making this the game default.
- **Shared host:** connect the collection's SDL input/config and the existing sound
  stream to the same window lifecycle. The native Win32 window is an isolated
  presentation probe, not a second player-facing frontend.

Current evidence: `.local/v090/ps2-gs-test-r23.log` (three contracts),
`graphics-window-r5` (resize and preserved 4:3 aspect), and `graphics-window-r6`
(1200 frames through the owned queue, zero normal VRAM readbacks; peak 4 MiB /
12 commands), `graphics-window-r7` (forced GPU sampled-image path, FIFO,
1200 frames and zero normal VRAM readbacks), and `graphics-window-r8` (final field
command, normal Alt+F4, pending producers released). Runtime mechanisms also pass
all nine tests in `ps2-native-r61.log`. These authored tests use no ROM or BIOS and do not establish game
compatibility or a game performance claim.

## Developer entry points

```powershell
pwsh -File scripts/Build-Ps2Gs.ps1 -Jobs 2
tools/apps/cmake/bin/ctest.exe --test-dir out/ps2-gs --output-on-failure
# Optional visible authored workload: frame count, present policy, transfer path.
out/ps2-gs/ps2_gs_window_probe.exe 1200
out/ps2-gs/ps2_gs_window_probe.exe 1200 fifo shader
```

The window probe produces commands on one thread and consumes them on the window
owner. Follow the same initialization handshake when attaching a queued backend
to the game: the producer may wait for its initial VRAM transfer, so the window
owner must already be pumping. On shutdown, close the transport before joining a
producer that could be waiting for a reply. GPU consumers must not call back into
locked frontend state; completion events cross back through an explicit mailbox.
