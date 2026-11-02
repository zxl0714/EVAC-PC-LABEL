# PS2 development tools

Run commands from the repository root. `scripts/Build-Ps2.ps1` restores the
dependencies pinned by `tools/ps2-source.lock.json` and selects the collection's
LLVM, CMake, Ninja and Windows SDK. It uses two compiler jobs by default.

```powershell
& scripts/Build-Ps2.ps1 -ToolsOnly
py -3.12 scripts/ps2_disc.py '<game.iso>' --output '<new-disc-report.json>' --extract-boot '<new-boot.elf>'
```

Compile the Ibara boot program against that same runtime and toolchain:

```powershell
& scripts/Build-Ps2.ps1 -IbaraGenerated '<verified-generated-directory>'
```

The pinned generator uses SSE4.1 intrinsics for Ibara's VU0 operations. Its
generated-code target selects this fixed CPU baseline, without AVX, AVX2 or
machine-specific instruction selection. ESPGaluda's overlay inputs are separate
options on the same build script; generated code is never shared across games.

Keep private game inputs, generated code and analysis projects under the ignored
development directories. The disc inspector reads ISO9660 extents and records
the boot ELF, segments and file inventory. Module preparation uses
`scripts/ps2_module.py`; code generation and its report validation use
`scripts/ps2_codegen.py`.

## Shared internal-entry generation

Set `compact_entries = true` in the recompiler configuration's `[general]`
section, then run `scripts/ps2_codegen.py` as usual. The common preparation
step folds nested `entry_*` ranges into their owning original function and
keeps their callable PCs in the generator's resume table. Standalone entries,
native bindings and branch-delay-slot entries retain independent bodies.
ESPGaluda enables this for its base modules; Ibara can use the same option.

The receipt's `compaction` section records original/shared body counts,
instruction ranges, tool/input hashes and the generated-entry verification.
For analysis without running the recompiler:

```powershell
py -3.12 scripts/ps2_compact.py '<config.toml>' --output '<new-analysis-directory>'
```

This writes a derived function map, configuration and report; the input map,
ELF and original generated files stay unchanged. `ps2_compact_contract` builds
and executes authored baseline/compact programs to compare entries, calls,
yield/resume, likely branches, delay slots and loops without game data.

## Analysis entries

Use Ghidra's Emotion Engine processor and the pinned upstream function exporter.
An overlay's own entry point does not identify every callable routine: the boot
program may call additional functions inside its address range.

`ghidra/ExportExternalCalls.java` exports flow references from the caller program.
Arguments are a new CSV path, the target module's start address and its exclusive
end, in `0x` notation. Review these targets against the module's function map.
`ghidra/SeedEntries.java` adds verified missing functions from a file containing
one hexadecimal address per line; it rejects targets inside existing functions.
Export the updated function map after seeding.
For reviewed callback-table candidates, `--skip-owned <new-report.csv>` leaves
interior addresses untouched and records their owning functions for inspection.

For additional boot or mode entries, `scripts/ps2_module_extras.py` validates an
independent generation against the base image and emits build glue. Its private
manifest selects `base`, `generated`, `map`, `baseReceipt`, `receipt`, and module
`id` 0 (boot), 16 (ARCADE), or 17 (ARRANGE). A manifest can contain a `modules`
array to build several inventories together. It compiles the explicit new bodies and installs their resume PCs
in empty slots of the same module table. Existing entries cannot be replaced.
Pass the manifest to `Build-Ps2.ps1 -ModeExtrasManifest` together with the boot,
ARCADE, ARRANGE and stage build inputs.
If generation needs the ELF entry row as an anchor, list its already-linked PC
in `baseEntries`; it is validated against the base table and reuses that body.
With all three inventories present, `checkLinks: true` checks additional fixed
branch targets against the boot and active-mode tables before building.
`linkedTables` supplies any separately compiled stage tables. This catches
missing direct entries; callback and virtual-table targets require data analysis.

## Incremental builds and diagnostics

After changing only runtime or host C++, reuse the configured tree:

```powershell
. scripts/Build-Common.ps1
Initialize-CollectionBuildEnvironment
& tools/apps/cmake/bin/cmake.exe --build out/ps2-tools --parallel 2
& tools/apps/cmake/bin/ctest.exe --test-dir out/ps2-tools --output-on-failure
```

Changing generated directories, module manifests or CMake options requires
configuration again. Keep the active build's log and session in `AGENTS.md`;
large translation units may take time without producing another log line.
Compilation batches cache root-symbol references against file names, sizes and
modification times. Editing a generated body invalidates its batch's cache.

Private gameplay iterations can pass `-FastGameLink` to omit generated-game PDB
linking. The normal build includes full symbols; use it for final validation
and packaging so crash reports match the shipped executable.

The development host accepts `boot.elf disc.iso extracted-disc-root test-profile`,
with optional `--frames N` and `--trace-boot`. Use a fresh profile for each run.
`--frame-times <new.csv>` buffers up to 36,000 presentation-return timestamps
and writes them after normal shutdown. `--coarse-timer` disables the shared
Windows timer request for a controlled diagnostic comparison; normal gameplay
uses the same scoped 1 ms request as the 360 host. These timestamps describe
host presentation returns, rather than monitor scanout.
`--capture-audio <new-directory>` keeps the latest 90 seconds of each SPU2 core
and the mixed output in bounded memory, then writes three stereo WAV files on
exit. `--trace-audio` adds bounded sample-bank, playback, volume and ADX-error
events. These diagnostics remain disabled during normal gameplay.
`--input-trace <new.csv>` buffers input and graphics event ordering; its default
200,000 records can be raised with `--input-trace-events` up to 1,000,000.
Check the saved overflow count before analysing an interval.
`--capture-gs <new.evacgs>` records from the initial graphics state. Its default
512 MiB budget can be raised with `--capture-gs-mib` up to 4096; the 18,000-field
limit remains in place. Offline replay accepts `--trace-limit-mib` for a larger
file. These options affect only explicitly enabled diagnostic capture.
`--probe-adx-coefficients boot.elf` checks the original ESPGaluda predictor headlessly at three sample
rates; it creates no game window.
`--probe-disc-index boot.elf` runs the original index parser headlessly against
authored inputs, including forced scheduler checkpoints. `libc_probe/` provides
an optional headless build for the original memory/string translations, using
`PS2_LIBC_GENERATED` to select a private generation.

Read the root `MEMORY.md` before controlling a game window. Runtime contracts,
generated-code validation and observed gameplay are separate acceptance checks.

`ps2_gpu_wait_probe` is a headless Windows experiment built by the same graphics
toolchain. It compares Vulkan completion waits with a D3D12 shared-fence event,
using Granite's external-timeline import API on the same adapter. Both paths
observe the same GPU submission; every sample checks its output and an already
completed fence value. It reports ABBA batches after warmup and changes no game
setting. Build the target in `out/ps2-tools` and run `bin/ps2_gpu_wait_probe.exe`.
This measures a transfer workload's completion wait, rather than game latency or
display presentation. API references: [Vulkan timeline interop](https://docs.vulkan.org/refpages/latest/refpages/source/VkD3D12FenceSubmitInfoKHR.html)
and [SetEventOnCompletion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12fence-seteventoncompletion).
