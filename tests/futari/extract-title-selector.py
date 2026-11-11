"""Build the actual generated selector in isolation; never copy game code to Git."""
from pathlib import Path
import sys

source = Path(sys.argv[1]).read_text(encoding="utf-8")
marker = "DEFINE_REX_FUNC(sub_82190A90) {"
start = source.index(marker)
end = source.index("\n}\n", start) + 3
Path(sys.argv[2]).write_text(
    '#include "futari_init.h"\n'
    'extern void FutariSelectLocalController(PPCRegister&);\n' + source[start:end],
    encoding="utf-8")
