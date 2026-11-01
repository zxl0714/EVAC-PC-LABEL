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
