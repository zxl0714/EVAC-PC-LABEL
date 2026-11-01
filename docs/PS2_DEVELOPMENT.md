# PS2 development

ESPGaluda uses the collection's pinned LLVM, CMake and Ninja toolchain. Restore
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

The first build restores the locked PS2 sources, builds the analysis tools,
prepares the original program and overlays, generates native C++, and builds
`out/ps2-tools/bin/espgaluda.exe` with its full PDB. Subsequent runtime changes
can use the existing generated inputs:

```powershell
./scripts/Build-Espgaluda.ps1 -NativeOnly
```

## Source layout

| Location | Purpose |
| --- | --- |
| `games/espgaluda/modules.json` | Original overlay identities and callback tables |
| `games/espgaluda/analysis/` | Reviewed function maps and generation options |
| `games/espgaluda/sdk_bindings.cpp` | Native bindings for the original SDK entry points |
| `third_party/ps2/runtime/` | Adapted EE, IOP, memory, timing and SDK implementations |
| `backends/ps2/` | Native window, Vulkan graphics, audio, input and collection integration |
| `tools/ps2/` | Shared runtime and game CMake targets |
| `.local/ps2/espgaluda/` | Fingerprinted ISO analysis and generated build inputs |
| `out/ps2-tools/` | Compiled tools, runtime libraries, EXE and matching PDB |

The analysis maps contain names and addresses. Game code is generated locally
from the supplied ISO. Changing a supported image or a callback table starts
with updating the analysis and its checksums; generated C++ stays in the build
directories.

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
