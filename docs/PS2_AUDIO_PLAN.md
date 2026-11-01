# PS2 audio integration

## Current path

`PS2AudioBackend::onSoundCommand` currently accepts SID `0x80000701` and treats
selected RPC payloads as sample playback requests. The IOP `libsd` HLE service
routes that same SID. This does not establish coverage of ESPGaluda's sound
driver; capture its actual RPC and hardware activity before selecting a bridge.

The opt-in traces in `play-r36` through `play-r39` now confirm RPC SIDs `0x12346`
and `0x90000200`, nonzero SPU2 key-ons on core 1, and core 0 streaming DMA through
the original SDR/EzMIDI/CRI drivers. The `0x80000701` heuristic does not cover
this path. Register/DMA integration with the isolated DSP is the selected bridge.

The adapted `IopMemory` currently stores SPU2 register values and simulates DMA
completion. It does not copy those DMA transfers into sound RAM or synthesize
voices. The shared IOP allocator fixes the sound upload allocation failure,
but audio output remains unaccepted.

## Candidate component

Audit [Play!'s SPU2 implementation](https://github.com/jpd002/Play-/tree/1f27232772e484bfb79364b751b84f41f4c87481/Source/iop)
at `1f27232772e484bfb79364b751b84f41f4c87481`. Its Framework submodule is pinned
to `587f278917acc0026bf5fc34b39f995fc26bd015`. Both licenses accompany the component under `third_party/ps2/spu/`. The source
lock now restores these revisions; no new component is linked into gameplay yet.

The relevant DSP sources are `Iop_SpuBase`, `Iop_Spu2_Core` and `Iop_Spu2`.
They depend on small Framework utilities plus logging and state serialization.
Keep the DSP isolated from PlayCore, its CPUs, graphics, frontend and network
dependencies. State serialization is separate from the game's memory-card
service and need not be included in the DSP adapter.

`scripts/ps2_spu_sources.py` verifies the pinned input digests and prepares only
three DSP translation units plus their utility headers. Its build-local view
removes six emulator-state methods and frontend logging dependencies. The
original dependency checkouts remain unchanged.

The isolated build passed six upstream SPU behavior tests and the collection
contract: all 48 voices individually sustain and release; both cores transfer
sample data across RAM wrap and DMA throttling; cross-core IRQ and acknowledgement
work. `ps2_spu_contract --bench` provides an authored 1/24/48-voice workload.
These checks establish component behavior, not game audio acceptance.

## Integration and verification

1. Add an opt-in, bounded trace of RPC SIDs/functions, nonzero key-on writes,
   SPU DMA source/size/direction, voice state and PCM queue activity. Keep normal
   gameplay and performance measurements free of this trace.
2. Restore the chosen sources through the project's locked dependency and
   compiler workflow. First build an isolated DSP contract and micro benchmark.
   Include authored ADPCM, pitch, ADSR, repeat, DMA and per-core IRQ cases; the
   upstream `tools/SpuTest` contains useful behavioral tests.
3. Model the separate 2 MiB sound RAM, two cores, sample cache and IRQ watcher.
   Route 16-bit SPU2 accesses directly, avoiding read/modify/write side effects
   on paired registers. Connect channel 4/7 DMA, transfer direction, AutoDMA,
   completion and IRQ semantics to the IOP.
4. Generate 48 kHz stereo PCM into a bounded host queue. Reconcile IOP cycles,
   EE counter units and VBlank timing before choosing a render step; do not
   infer the audio clock from the development window's draw counter.
5. Provide the mixer output through a small subsystem API, preserving one
   consistent IOP header view. Avoid changing `PS2Runtime`'s layout merely to
   expose an audio queue; generated-game PCHs must keep the same ABI.
6. Check menu, character selection, first-stage music and simultaneous effects
   in ESPGaluda. Measure nonzero PCM, queue depth and underruns, then confirm
   audible output. Reuse the platform path for Ibara's different driver.

For presentation, distinguish framebuffer pixel dimensions from television
aspect. The development host now presents the two PS2 titles at 4:3; compare
[PCSX2's aspect settings](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/Config.h)
when extending that policy.

## Bridge constraints

- Keep `SpuDevice` and its sound RAM on the IOP owner thread. The device consumer
  only reads the stereo PCM queue. The existing `PcmQueue` contract covers
  concurrent wrap, full-buffer pressure and producer-requested discard on IOP
  reset; do not reset both indices while the device callback is running.
- Route SPU accesses at their native 16-bit width. Paired read/modify/write
  operations can retrigger key-ons or write the transfer FIFO twice. Preserve
  DMA busy state until all requested bytes transfer; AutoDMA must resume after
  the DSP consumes input. Deliver both DMA-channel and SPU IRQ callbacks.
- Advance sound while guest callbacks/startup code execute as well as in the
  normal IOP loop. A driver can wait for DMA inside such a callback. Keep callback
  delivery guarded against recursive IRQ service.
- The scheduler's EE clock is 294,912,000 Hz and its IOP conversion divides by
  eight. Separate guest VBlank from the host's draw counter. The new `vsync=`
  probe field already shows that the development host can redraw at 60 Hz while
  guest time advances more slowly. Game pace remains unaccepted; do not conceal
  it by generating audio from the window draw counter.
- Public IOP changes must have the same declaration in every source view.
  Prefer the existing `runtime-include` and `iop-source` search paths for adapted
  headers. Adding a new transitive include directory can invalidate all generated
  game PCHs and trigger a needless full compile.
- Expose PCM through small `IopMemory` / `IopEmulator` / `IopSubsystem` methods,
  retaining the runtime PIMPL boundary. Keep `PS2Runtime`'s layout stable. Raylib
  already provides a miniaudio output device; avoid importing another frontend
  or the PS4 runtime merely to play stereo PCM.
- Preserve Play!'s core-1 enable condition before rendering/mixing that core.
  The isolated 1/24/48-voice workload retains the same PCM checksums with that
  condition; it avoids rendering a disabled core.
