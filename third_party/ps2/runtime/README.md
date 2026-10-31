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
