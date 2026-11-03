# PS2 development

ESPGaluda and Ibara use the collection's pinned LLVM, CMake and Ninja toolchain. Restore
the workspace tools with `scripts/Prepare-Tools.ps1`, then build from an original
SLPS-25352 ISO:

```powershell
./scripts/Build.ps1 -Games espgaluda -DataDirectory 'E:\MyGames\Espgaluda'
```

The data directory can contain the ISO under its original filename. The PS2
build entry also accepts an ISO path:

```powershell
./scripts/Build-Espgaluda.ps1 -DataDirectory 'E:\MyGames\Espgaluda (Japan).iso'
```

Build Ibara from a Japanese SLPM-66301 ISO:

```powershell
./scripts/Build.ps1 -Games ibara -DataDirectory 'E:\MyGames\Ibara'
./scripts/Build-Ibara.ps1 -NativeOnly
```

Both games share the runtime and build graph. Building either game preserves
the other game's configured inputs; native-only builds refresh both executables
when shared runtime code changes.

The first build restores the locked PS2 sources, builds the analysis tools,
prepares the original program and overlays, generates native C++, and builds
`out/ps2-tools/bin/espgaluda.exe` with its full PDB. Subsequent runtime changes
can use the existing generated inputs:

```powershell
./scripts/Build-Espgaluda.ps1 -NativeOnly
```

If a shared-header change relinks the generator, the native-only build validates
the new generator in a separate output directory. It reuses compiled game
objects only when every generated C++ and header is byte-for-byte identical.
The original generation receipts and file timestamps stay intact; an additional
equivalence receipt records the fresh validation. Changed game bodies require
the normal full build.

The normal build certifies its source, toolchain, locked dependencies and
generated input set, checks both EXE/DLL against their matching PDBs, then stages
the player files under `runtime/ps2/`. The private symbols remain available for
debugging; player packages omit them. The certificate is
`out/ps2/build-receipt.json`.

## Source layout

| Location | Purpose |
| --- | --- |
| `games/espgaluda/modules.json` | Original overlay identities and callback tables |
| `games/espgaluda/analysis/` | Reviewed function maps and generation options |
| `games/espgaluda/sdk_bindings.cpp` | Native bindings for the original SDK entry points |
| `games/ibara/analysis/`, `games/ibara/sdk_bindings.cpp` | Reviewed SLPM-66301 function map, generation settings and SDK bindings |
| `third_party/ps2/runtime/` | Adapted EE, IOP, memory, timing and SDK implementations |
| `backends/ps2/` | Native window, Vulkan graphics, audio, input and collection integration |
| `tools/ps2/` | Shared runtime and game CMake targets |
| `.local/ps2/espgaluda/` | Fingerprinted ISO analysis and generated build inputs |
| `.local/ps2/ibara/` | Fingerprinted Ibara analysis and generated build inputs |
| `out/ps2-tools/` | Compiled tools, runtime libraries, EXE and matching PDB |
| `scripts/ps2_build_receipt.py`, `scripts/stage_ps2.py` | Build certification and player runtime staging |
| `scripts/ps2_input_order.py` | Reviewed main-loop input order and generated-body identity checks |

The analysis maps contain names and addresses. Game code is generated locally
from the supplied ISO. Changing a supported image or a callback table starts
with updating the analysis and its checksums; generated C++ stays in the build
directories.

The ESPGaluda main loop keeps its original graphics wait, then reads and maps
input before updating the game. The build prepares the two affected bodies in
`out/ps2-tools/espgaluda-input-order/`; the original generated tree stays intact.
The build receipt verifies both transformed bodies and their original sources
under the `late-v1` policy before staging the executable.

## Checks and profiling

The independent mechanism graph can be built without game data:

```powershell
./scripts/Build-Ps2.ps1 -RuntimeChecks
./tools/apps/cmake/bin/ctest.exe --test-dir out/ps2-native-check --output-on-failure
```

IOP scheduling and EE timer microbenchmarks are available in the test binaries:

```powershell
./out/ps2-tools/bin/ps2_iop_memory_contract.exe --bench-dispatch
./out/ps2-tools/bin/ps2_iop_memory_contract.exe --bench-idle
./out/ps2-tools/bin/ps2_kernel_contract.exe --timer-bench
```

Use the normal collection entry for gameplay measurements. Its run directory
contains the configuration snapshot and buffered `frame-times.csv`, including
absolute QPC timestamps. F8 writes a timestamp marker. Additional audio,
instruction-pointer sampling and image readback belong in separate diagnostic
runs. See [MEMORY.md](../MEMORY.md) for the tested input and menu workflow.

The development CLI accepts `--input-trace <new.csv>` to record host key
transitions, guest pad reads, display-register writes and field/present IDs in
one buffered timeline. The trace is disabled by default and serialized after
the execution threads join. A `feedback_unknown_stage` row is a driver timing
observation whose presentation stage is not exposed by Granite's public API;
it is not proof of scanout.
`gs_command` rows carry the producer field, command kind (initialize/reset/GIF/
register/image/FIFO/snapshot/VRAM write/frame, numbered 0–8), elapsed microseconds
and payload bytes. They include synchronous GPU waits performed by that command;
the elapsed value is not a CPU-work measurement.
Driver feedback is a separate opt-in (`EVAC_PS2_PRESENT_FEEDBACK=1`);
input and pixel tracing do not require it.

For first-response image analysis, set `EVAC_PS2_PIXEL_TRACE` to a fresh output
prefix and `EVAC_PS2_PIXEL_REGION` to `x,y,width,height,frame_count` in the native
scanout image, then launch with input tracing. F9 starts the one-shot capture.
Use `0,0,0,0,96` to select the complete native scanout automatically.
The bounded ROI copies use the existing GPU submission and frame fence;
`.bin` pixels and `.csv` field IDs are saved after drain. Keep this diagnostic
run separate from ordinary CPU and frame-time measurements.

Held input in an independent diagnostic session can use the existing
`tools/latency/test_pad_client.py` protocol: set `EVAC_LATENCY_TRACE` to a fresh
trace file and `EVAC_LATENCY_TEST_PAD` to a unique pipe name, alongside
`--input-trace`. This is a process-local synthetic controller. Its published
timestamps can establish the guest-side input chain; they do not measure USB,
keyboard or SDL front-end latency. Ordinary launches leave it disabled.
