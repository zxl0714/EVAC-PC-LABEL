# PS2 runtime adaptations

`EeScheduler.cpp` is derived from [PS2Recomp e5636981](https://github.com/ran-j/PS2Recomp/tree/e5636981a1c9de368f6b71e819c3dc188dfedbf4),
`ps2xRuntime/src/lib/Kernel/EeScheduler.cpp`. It retains the upstream GPL version
3 license in `LICENSE`; the remaining runtime sources use the pinned checkout.

The local scheduler accepts EE priorities 0 through 127 for thread creation and
priority changes. ESPGaluda's CRI initialization copies the main thread's
priority into its worker descriptors before starting them. Rejecting zero makes
every worker creation fail with `KE_ILLEGAL_PRIORITY`.

`Thread.cpp` comes from the same commit's `Kernel/Syscalls/Thread.cpp`. Priority
change services return the previous priority on success, so CRI critical sections
restore their caller's real priority instead of permanently promoting it to zero.

The range agrees with the EE kernel implementation in
[Play! PS2OS.cpp](https://github.com/jpd002/Play-/blob/master/Source/ee/PS2OS.cpp).
`ps2_kernel_contract` tests the guest ABI, inherited priority, both boundaries,
and invalid values without game data.

`ps2_runtime.h` and `ps2_runtime.cpp` come from the same commit's `include/` and
`src/lib/`. They add per-runtime static module registration and lookup through
`backends/ps2/static_modules.*`. The original MWo3 loader header selects native
code for overlays sharing the same guest addresses. Unknown or mismatched
headers cannot select a bank. Re-loading an ELF clears the registrations.
When an original synchronous loader replaces its caller's bank, native call
dispatch unwinds at the preserved guest PC and resolves the new bank before
continuing. The headless kernel regression exercises this transfer.

CMake stages one complete runtime include tree with the adapted header. Both
direct and quoted sibling includes therefore use the same class layout.

`ps2_runtime_macros.h` wraps the pinned upstream macro header, staged as
`ps2_upstream_runtime_macros.h`. The five widths of READ and WRITE share named
functions marked noinline, avoiding repeated address handling in generated
bodies. Constant-address FAST accesses retain their inline implementation.
Special-address dispatch, RAM wrapping, scratchpad handling and access widths
retain their upstream behaviour. `ps2_memory_access_contract` checks reads and
writes of all widths, RAM aliases, boundary wrapping, scratchpad, adjacent bytes,
address evaluation and preserved guest state.

Thirteen dynamic integer load/store instructions also share address calculation
and register writeback. The generated call packs the source register, target
register and signed offset into one argument. Each instruction helper inlines
its access body; ordinary READ/WRITE callers still use the outlined access
functions. Generated PC and delay-slot tracking stays at the original caller.

Generated dispatch arrays are zero-initialized and populated before `main`
from run descriptions by `backends/ps2/function_table.*`. This keeps the large
dense arrays in BSS while normal dispatch and module extensions still use the
same writable pointer arrays. `scripts/ps2_function_table.py` creates build
views without editing original generated sources; build certification verifies
each view. The native contract checks holes, indirect calls, separate banks
and invalid ranges.

`iop_emulator.cpp` supplies the IRX startup `argc`/`argv` interface, including the
module path and a null table terminator. `iop_sysclib.cpp`, adapted from
`ps2xIOP/src/emulator/imports/iop_sysclib.cpp` at the same revision, implements
the PS2 ASCII classification flags returned by ordinal 8. CRI ADX validates
numeric startup options through this API before selecting its SPU2 core.
The no-ROM IOP contract checks argument framing and character classification.

`ps2_pad.cpp` comes from the same commit's `src/lib/ps2_pad.cpp`. It combines
keyboard and controller buttons instead of disabling the keyboard whenever
gamepad 0 is connected. The host-input regression uses authored key/controller
states and calls the actual pad implementation.

`System.cpp` comes from `Kernel/Syscalls/System.cpp`. Automatic main stacks
start at the top of their allocation and report a stable lower bound through
`ReferThreadStatus`. The heap ceiling excludes that allocation. This corrects
the placement defect also described in [upstream PR 398](https://github.com/ran-j/PS2Recomp/pull/398).
Automatic stacks leave the kernel's top 4 KiB reserved and a `0x2a0` initial
context frame, matching [Play!'s EE kernel](https://github.com/jpd002/Play-/blob/master/Source/ee/PS2OS.cpp).
`SetupHeap` returns the upper boundary below the main stack. Kernel helpers and
bound guest allocation functions, including aligned allocation, share one heap;
its capacity includes the allocator's minimum bookkeeping reserve. This matters
to software that probes available heap space during initialization.
Asynchronous callback stacks occupy the kernel-owned gap from `0x90000` to
the lower of `0x100000` and the first loaded segment. Exhaustion fails explicitly.
The kernel regression checks placement, metadata and heap isolation.

Native calls resume their caller only when the guest PC actually reaches the
return address; a yield at the callee's entry keeps that entry pending. Execution
exceptions propagate after thread and window cleanup, giving the host a failure
result. `CD.cpp`, from `Kernel/Stubs/CD.cpp`, resolves original ISO9660 extents
through `backends/ps2/iso_disc.*`, retaining physical sector addresses.


`GS.cpp` is adapted from `Kernel/Stubs/GS.cpp` at the same pinned revision.
Graph reset writes CSR/IMR and CRT mode through the privileged MMIO ports;
it does not encode PMODE/DISPLAY as A_D drawing registers. This distinction
matters when the original raw GIF stream reaches a hardware renderer.
`System.cpp` programs SMODE1/SMODE2 for supported BIOS video-mode IDs and routes
SDK interrupt-mask writes through the same notification as guest MMIO.
Stable PLL/carrier encodings follow
[ps2sdk graph_mode.c](https://github.com/ps2dev/ps2sdk/blob/master/ee/graph/src/graph_mode.c).

`backends/ps2/crt_mode.h` isolates video-mode selection and the libgraph display
builder. DX/DW use CRT clock units; caller offsets are signed pixels, and
interlaced frame mode spans two fields. The display builder currently supports
NTSC, PAL and 480p; unknown modes fail explicitly. Other timing-register guest
writes remain intact. This does not emulate analog PLL settling or define a
host-window resolution. SDK arguments use the EE's first eight argument
registers; zero register values never fall back to stale stack contents.
The returned GParam uses four 16-bit fields. The headless native fixture checks
CRT-only reset, privileged/drawing separation, both interlace modes, PAL offsets,
zero argument values, GParam layout, and SDK interrupt unmask notification.


The desktop `PS2Runtime` owns a forward-declared `NativeHost` through a PIMPL.
The normal run path submits original GS commands to the Vulkan consumer and
retains the existing Raylib/miniaudio audio stream independently of a graphics
window. Physical controllers and keyboard configuration reuse the collection's
SDL3 input implementation. The SDL source revision is locked in
`tools/ps2-source.lock.json`. `host_input.h` supplies scoped shared pad-source
ownership so an in-flight EE read cannot outlive its device state. The CPU pad
fallback remains available to authored mechanism tests.
