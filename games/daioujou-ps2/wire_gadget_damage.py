"""Attach read-only gadget observation to the reviewed Death Label program."""

import re
import sys
from pathlib import Path


def wire_damage(source):
    # The guest writes the new enemy HP only after resolving a collision.
    # The remaining stores in this function handle positions and hit flags.
    for pc, operands, kind in (
        (0x23BD54, 0x00820018, "Shot"),
        (0x23BFF4, 0x00820018, "Aura"),
        (0x23C3F4, 0x00830000, "Laser"),
        (0x23CA14, 0x00830018, "Laser"),
        (0x23CD8C, 0x00830018, "Laser"),
    ):
        marker = (
            rf"(ctx->pc = 0x{pc:x}u;\n    )"
            rf"(::cave::ps2::Store16\(rdram, ctx, runtime, 0x{operands:08X}u\);)"
        )
        source, count = re.subn(
            marker,
            rf"\g<1>::cave::daioujou_ps2::ObserveDeathLabelDamage("
            rf"rdram, ctx, ::cave::daioujou_ps2::DeathLabelAttack::{kind}, "
            rf"0x{operands:08x}u);\n    \g<2>",
            source,
        )
        if count != 1:
            raise ValueError(f"Expected one reviewed damage store at {pc:#x}")
    header = Path(__file__).resolve().with_name("death_label_damage.h").as_posix()
    return f'#include "{header}"\n' + source


if __name__ == "__main__":
    source_path, output_path = map(Path, sys.argv[1:])
    output_path.write_text(wire_damage(source_path.read_text(encoding="utf-8")), encoding="utf-8")
