# PS2 recompiler adaptations

`vu_translation_helpers.cpp` is derived from
[PS2Recomp e5636981](https://github.com/ran-j/PS2Recomp/tree/e5636981a1c9de368f6b71e819c3dc188dfedbf4),
`ps2xRecomp/src/lib/vu_translation_helpers.cpp`. `fpu_translator.cpp` is adapted
from the same revision's `ps2xRecomp/src/lib/fpu_translator.cpp`. Other tool
sources are restored through `tools/ps2-source.lock.json`.

The local VCALLMSR implementation reads the CMSAR0 control register. The upstream
implementation indexed `vi[27]` although that array contains only 16 integer
registers. The reference register mapping and instruction behaviour are in
[PCSX2 VU.h](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VU.h) and
[COP2.cpp](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/COP2.cpp).

The native regression compiles and executes the translator's emitted C++ with
authored register values, including micro-memory address wrapping. It needs no
game data. The inherited GPL version 3 license is included in `LICENSE`.

R5900 `SQRT.S fd, ft` reads bits 20 through 16 of the instruction. The upstream
translator selected `fs`, producing an unrelated input for the ADX predictor
coefficient calculation. The local translator selects `ft`, matching
[Play!'s COP_FPU implementation](https://github.com/jpd002/Play-/blob/1f27232772e484bfb79364b751b84f41f4c87481/Source/COP_FPU.cpp).
The generated-C++ regression executes the original instruction encoding with
different values in `fs` and `ft`. The private game host also has a headless
coefficient probe for comparing the original routine with the ADX formula at
multiple sample rates.
