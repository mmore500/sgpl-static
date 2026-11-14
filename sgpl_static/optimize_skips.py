"""One optimizer run: which of these skips make no difference?

A "skip" is the decision not to run an instruction. In the saved IR each
instruction sits behind a skip() call; when the optimizer deletes that call,
the skip holds."""

import re
import subprocess

DECL = re.compile(r"^@sgpl_enabled = .*$", re.M)
SKIP = re.compile(r"call[^\n]*@skip\(([^)]*)\)")
LITERAL = re.compile(r"i32 noundef (\d+)")


def optimize_skips(
    saved_ir: str,
    size: int,
    maybe: set[int],
    nopped: set[int],
    opt_flags: list[str],
) -> set[int]:
    """The skips among `maybe` that hold together, given that the
    positions in `nopped` are skipped already.

    Every position in `maybe` is made "maybe skipped" in the saved IR, and
    `opt` is run with `opt_flags`."""
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
    calls_left = set()
    for argument in SKIP.findall(out):
        literal = LITERAL.fullmatch(argument)
        if literal is None:
            # the optimizer merged skip() calls, so which ones are left is
            # unknown: none of them holds
            return set()
        calls_left.add(int(literal[1]))
    return maybe - calls_left
