"""One pass: propose skips, then finalize the proposals."""

import random

from .finalize_skips import finalize_skips
from .log import log
from .propose_skips import propose_skips


def do_pass(
    saved_ir: str,
    size: int,
    todo: list[int],
    proven: set[int],
    opt_flags: list[str],
    jobs: int,
    number: int,
    rng: random.Random | None,
) -> set[int]:
    """The skips in `todo` that this pass proves, given that those in
    `proven` are made already. `number` labels the pass; `rng` is passed to
    finalize_skips."""
    proposed = propose_skips(
        saved_ir, size, todo, proven, opt_flags, jobs, f"pass {number} propose"
    )
    final = finalize_skips(
        saved_ir, size, proposed, proven, opt_flags,
        f"pass {number} finalize", rng,
    )
    log(f"pass {number}: proposed {len(proposed)}, finalized {len(final)}")
    return final
