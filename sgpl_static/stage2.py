"""Stage 2: set the skip switches in the saved IR and re-optimize with `opt`.
A position is dead if its skip() call does not survive."""

import re
import subprocess

DECL = re.compile(r"^@sgpl_enabled = .*$", re.M)
SKIP = re.compile(r"call[^\n]*@skip\(([^)]*)\)")
LITERAL = re.compile(r"i32 noundef (\d+)")

def kept(saved_ir, size, maybe, nopped, opt_flags):
    """Positions whose skip() call survives. `maybe`: positions that may be
    skipped; `nopped`: positions always skipped; the rest always run.
    `opt_flags`: flags for `opt`, which name the passes to run."""
    table = "".join(
        "\\02" if i in nopped else "\\01" if i in maybe else "\\00"
        for i in range(size)
    )
    text = DECL.sub(
        f'@sgpl_enabled = internal constant [{size} x i8] c"{table}", align 16',
        saved_ir,
    )
    out = subprocess.run(
        ["opt", "-S", *opt_flags],
        input=text, capture_output=True, text=True, check=True,
    ).stdout
    surviving = [LITERAL.fullmatch(argument) for argument in SKIP.findall(out)]
    if not all(surviving):
        # the optimizer merged skip() calls, so their positions are unknown:
        # count every position that may be skipped as surviving
        return set(maybe)
    return {int(match[1]) for match in surviving}
