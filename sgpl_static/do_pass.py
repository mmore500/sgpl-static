"""One pass: propose skips, then finalize the proposals."""

from .finalize_skips import finalize_skips
from .log import log
from .propose_skips import propose_skips


def do_pass(saved_ir, size, todo, proven, opt_flags, jobs, number, rng):
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
