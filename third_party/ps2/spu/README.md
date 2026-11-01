# SPU2 DSP component

The source lock restores Play! and its Framework dependency. Only three SPU DSP
translation units and seven utility headers are compiled. The collection's
`SpuDevice` adapter owns sound RAM, registers, DMA and stereo rendering.

`scripts/ps2_spu_sources.py` verifies every input digest and creates a build-local
source view. It removes six emulator-state serialization methods and their ZIP
dependencies, retaining the voice, ADPCM, ADSR, reverb and IRQ implementation.
The release logging implementation is retained without the frontend logger.
Game memory-card saves use a separate service.

The exact source inventory is in `sources.json`; upstream revisions are in
`tools/ps2-source.lock.json`. `LICENSE_PLAY.txt` and `LICENSE_FRAMEWORK.txt` must
accompany binaries that include this component.
