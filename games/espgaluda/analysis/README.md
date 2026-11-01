# ESPGaluda analysis maps

The manifest describes the boot program, fifteen stage overlays, ARCADE and
ARRANGE. Eighteen base maps and thirteen extension maps record function names,
address ranges, code labels and sizes. CSV files use deterministic gzip
compression; their uncompressed SHA-256 and row counts are in `manifest.json`.

`scripts/ps2_game.py` reads the original SLPS-25352 ISO, reconstructs each
analysis ELF, checks its checksum, and generates C++ with the pinned recompiler.
The root names and callback inventories preserve the reviewed linkage between
overlays. Normal development builds use these maps directly; Ghidra is used
when changing the analysis.

See [PS2 development](../../../docs/PS2_DEVELOPMENT.md).
