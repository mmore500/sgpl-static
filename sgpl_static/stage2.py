"""Stage 2: set the skip switches in the saved IR and re-optimize with `opt`.
A position is dead if its skip() call does not survive."""

import re
import subprocess

DECL = re.compile(r"^@sgpl_enabled = .*$", re.M)
KEPT = re.compile(r"call[^\n]*@skip\(i32 noundef (\d+)\)")

def kept(saved_ir, size, maybe, nopped, pipeline):
    """Positions whose skip() call survives. `maybe`: positions that may be
    skipped; `nopped`: positions always skipped; the rest always run.
    `pipeline`: what `opt -passes=` accepts."""
    table = "".join(
        "\\02" if i in nopped else "\\01" if i in maybe else "\\00"
        for i in range(size)
    )
    text = DECL.sub(
        f'@sgpl_enabled = internal constant [{size} x i8] c"{table}", align 16',
        saved_ir,
    )
    out = subprocess.run(
        ["opt", "-S", f"-passes={pipeline}"],
        input=text, capture_output=True, text=True, check=True,
    ).stdout
    return set(map(int, KEPT.findall(out)))
