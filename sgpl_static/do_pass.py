"""One pass: propose skips, then finalize the proposals."""

from .finalize_skips import finalize_skips
from .log import log
from .propose_skips import propose_skips


def do_pass(saved_ir, size, todo, proven, opt_flags, jobs, number):
    """The skips in `todo` that this pass proves, given that those in
    `proven` are made already. `number` labels the pass."""
    proposed = propose_skips(
        saved_ir, size, todo, proven, opt_flags, jobs, f"pass {number} propose"
    )
    final = finalize_skips(
        saved_ir, size, proposed, proven, opt_flags, f"pass {number} finalize"
    )
    log(f"pass {number}: proposed {len(proposed)}, finalized {len(final)}")
    return final
