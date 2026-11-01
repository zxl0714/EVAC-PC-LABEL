# PS2 graphics architecture

Status: raw-command, GPU pixel/transfer and bounded-queue contracts passed on
RTX 4080. Direct GPU window output, resize and two-thread submission have also
passed authored workloads. NativeHost now runs ESPGaluda menus, character
selection and its tutorial through Vulkan; gameplay/audio acceptance remains
open. This plan takes
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
  FIFO/snapshot/reset requests return through ordered replies. EE VBlank offers
  a frame without blocking: one field may be outstanding, and the dispatcher
  retains a deferred VBlank until a completion/capacity event wakes it. The EE
  must remain able to execute GS IRQ handlers that acknowledge SIGNAL. Errors
  and shutdown release every synchronous waiter.
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
- Submit PGS's pending texture transfers before `WSI::begin_frame()`. Granite
  waits for recorded command buffers before advancing allocation contexts;
  entering that wait before PGS flushes its uploads would wait on our own work.
  The window regression uploads a texture before every presented field, so it
  covers this boundary as well as register-only drawing. This uses submission,
  not a device-wide idle wait.

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

## Completion and interrupt model

The implementation separates CPU command parsing, GPU work completion and EE
status. This follows the responsibilities visible in
[PCSX2's GIF handling](https://github.com/PCSX2/pcsx2/blob/da6bae1050843d47ef1a8c00a1708f0cfb9a0ff5/pcsx2/Gif_Unit.cpp)
and [CSR/IMR handling](https://github.com/PCSX2/pcsx2/blob/da6bae1050843d47ef1a8c00a1708f0cfb9a0ff5/pcsx2/GS.cpp),
while using paraLLEl-GS for the renderer itself.

- The adapter flushes preceding commands and waits on their Generic-queue fence
  before publishing FINISH/SIGNAL. It does not use device-wide idle or scanout
  readback. These explicit status markers can complete without a window frame.
- A fixed-size mailbox coalesces sticky FINISH and ordered masked LABEL updates.
  A second SIGNAL waits for the EE CSR acknowledgement; reset and close release
  the wait. A message-pump callback keeps the owner window responsive while it
  waits. Mailbox notification coalesces one unread batch into one EE wake.
- Every queued command retains its producer generation. Reset advances the
  generation before its own queue submission, so an owner stalled in an older
  command can unwind; stale callbacks cannot populate the new generation.
- The EE merges SIGLBLID/CSR, applies interrupt masks and acknowledges status.
  Byte, halfword and word MMIO accesses share the same boundary. IMR unmask can
  raise an existing pending source. The GPU owner never writes guest registers.
- Runtime MMIO callbacks and scheduler polling use the EE executor; an explicit
  `GsStatus` event wakes an idle executor. A generation-based thread-local owner
  identity rejects foreign GUI/debug dispatch.

Evidence: `ps2-gs-test-r29.log` covers the real GPU path including a queued reset
behind a blocked second SIGNAL; `ps2-native-r64.log` includes native MMIO and
sleeping-EE-to-IRQ dispatch. These components still need actual game integration.

## Integration gates still open

- **Game event connection:** the completion mailbox, GPU fences, GS MMIO status
  handoff and idle-EE IRQ wake have mechanism coverage. NativeHost now connects
  its mailbox notifier to `EeEventType::GsStatus` before starting the producer.
  Actual game waits and exit still need integrated acceptance.
- **Offline replay:** the bounded `.evacgs` recorder and decoded workload are
  implemented. Attach recording at the initial VRAM handoff, before any draw.
  A mid-game VRAM-only snapshot is incomplete: registers, GIF continuations and
  transfer state also matter. Actual game capture remains an integration gate.
- **Production WSI:** direct blit checks the source/destination format features
  and swapchain transfer-destination support. A GPU sampled-image path uses the
  original renderer's checked-in shaders when direct blit is unavailable; its
  forced path has passed the authored window test. Verify fullscreen/minimize,
  device loss and unsupported capabilities before making this the game default.
- **Shared host:** the integrated NativeHost now connects the shared SDL
  input/config and the existing sound stream to the same lifecycle. Complete
  actual gameplay/input/audio acceptance before player packaging.

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

## Capture and offline measurement

`GsRecordingBackend` records the original three paths, direct SDK register/image
operations, explicit CPU VRAM writes, FIFO replies and complete field snapshots.
The initial 4 MiB comes from the producer's existing initialization handoff.
Recording adds no VRAM readbacks. Byte and frame limits finalize a valid prefix
while live drawing continues; output creation never overwrites an existing file.

The versioned `.evacgs` format is collection-owned. A synthetic A_D packet on an
arbitrary path could corrupt a partially received original GIF tag, so native
SDK writes remain distinct operations. The decoder validates the entire bounded
file before executing it. Each reusable workload owns its payloads and FIFO
scratch; measured replay performs no file I/O or packet-buffer allocation.

```powershell
# Create an authored trace without a game or BIOS; choose a new output name.
out/ps2-gs/ps2_gs_trace_gpu_contract.exe --keep .local/authored.evacgs
out/ps2-gs/ps2_gs_replay.exe .local/authored.evacgs --seconds 2 --warmup 2
```

The replay tool measures wall time and aggregate process CPU time, reports cost
per field and equivalent CPU cores, and verifies recorded FIFO replies. It
includes reset and initial VRAM upload for every iteration; it excludes EE, IOP,
input, window presentation and disk decoding. It runs uncapped. This is not a
game frame rate or CPU-usage result. The tool disables speculative background
shader precompilation in its own process and warms the workload's actual
pipelines before measurement. Normal game initialization retains precompilation.

Offline replay disables EE completion callbacks: the recorded command order
already reflects the original SIGNAL acknowledgements, which are not graphics
commands. Live gameplay retains the mailbox and interrupt protocol.

Evidence: all six GS contracts pass in `.local/v090/ps2-gs-test-r33.log`.
The GPU capture test checks three interleaved partial paths, native shortcuts,
FIFO data and repeated field scanout, then compares the entire 4 MiB VRAM exactly.
The final verification readbacks are diagnostic-only. The authored benchmark
runs in `ps2-gs-bench-r2.json` without a window or normal-path VRAM snapshots.

## CRT and SDK boundary

The SDK and firmware service adapter must preserve the same hardware boundary
as original code. PMODE, SMODE, DISPLAY and DISPFB use privileged MMIO; they are
not aliases for A_D drawing registers. The adapted graph reset no longer emits
an invented GIF packet that writes SCISSOR_2/ALPHA_1 instead of display state.

CRT mode IDs, stable clock/carrier fields, libgraph magnification/offset units,
EE argument registers and GParam layout now have a headless native regression
(`ps2-native-r65.log`, all nine tests pass). Ordinary generated game code still
writes its own privileged display registers; the GPU scanout consumes their
complete field snapshot. No framebuffer size or guest display mode is inferred
from the player's chosen host-window size.

## Integrated native host

`NativeHost` now connects the shared runtime to the Vulkan consumer. Its PIMPL
keeps graphics/window headers out of generated game headers. The EE attaches a
shared queue proxy while the main thread is already pumping, including the
initial 4 MiB upload handshake. The completion mailbox posts `GsStatus` events
to the EE scheduler. Callback execution and guest register ownership stay on EE.

The window owns presentation, focus and keyboard events. SDL3 uses the same
pinned input implementation and keyboard parser as PS4; it owns physical devices
only. Host pad reads retain short key edges, combine keyboard/controller state,
and acknowledge delivered SDL edges. The legacy Raylib pad path remains only for
small fallback tests. The normal host keeps the existing miniaudio/SPU2 stream
without creating an OpenGL window or converting frames through CPU RGBA.

Closing the host first closes the queue/mailbox, then joins the EE. The shared
proxy can safely outlive the GPU owner during runtime destruction. Shutdown does
not request a backend-to-CPU snapshot. Typed cancellation is separate from a
guest or device failure; real EE exceptions propagate after cleanup. Wait-pump
callbacks also notice an EE stop while waiting for SIGNAL/GPU completion.

Current evidence:

- `.local/v090/ps2-native-r67.log`: all 15 native/GS checks pass.
- `ps2-native-host-r2`: actual runtime/EE -> Vulkan window, 10 fields, standalone
  audio stream, normal completion, zero normal-path VRAM readbacks.
- `ps2-native-host-fault-r1`: authored EE failure after three fields propagates
  after transport/thread/audio cleanup.
- `ps2-native-host-window-r2`: computer-use F11 fullscreen and restore observed;
  4:3 scanout retained. It reached its authored 3600-field limit before Alt+F4,
  so this run does not establish user-close cancellation.

All authored host probes use no ROM or BIOS. Actual game rendering, input,
audio pacing, minimize/device-loss, and user-close acceptance remain required.
Use `Build-Ps2.ps1 -RuntimeChecks` for the independent no-ROM build graph; it does
not reconfigure or rebuild the large game graph. The visible integration probe
is an explicit optional executable, not an automatically opened CTest window.
