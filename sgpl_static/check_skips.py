"""One optimizer run: which of these skips make no difference?"""

import re
import subprocess

DECL = re.compile(r"^@sgpl_enabled = .*$", re.M)
SKIP = re.compile(r"call[^\n]*@skip\(([^)]*)\)")
LITERAL = re.compile(r"i32 noundef (\d+)")

def check_skips(saved_ir, size, maybe, nopped, opt_flags):
    """The positions among `maybe` that can be skipped without any observable
    difference, given that those in `nopped` are skipped already.

    Every position in `maybe` is made "maybe skipped" in the saved IR, and
    `opt` is run with `opt_flags`. A skip makes no difference if the optimizer
    deleted its skip() call; the instruction is then dead."""
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
        # the optimizer merged skip() calls, so which ones survive is unknown:
        # pass none of them
        return set()
    return set(maybe) - {int(match[1]) for match in surviving}
