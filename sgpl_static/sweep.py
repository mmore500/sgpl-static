"""The sweep: find a set of skips that hold together."""

import sys

from .finalize_skips import finalize_skips
from .propose_skips import propose_skips


def sweep(saved_ir, candidates, size, opt_flags, jobs, max_passes):
    """Returns the positions among `candidates` proven dead together.
    `max_passes`: 0 for as many passes as it takes."""
    proven = set()
    # Each pass retries everything still unproven on top of the passes before
    # it: some positions are only dead once others are gone. A pass that
    # proves nothing leaves nothing for the next one to build on.
    number = 0
    while not max_passes or number < max_passes:
        number += 1
        todo = sorted(set(candidates) - proven)
        proposed = propose_skips(
            saved_ir, size, todo, proven, opt_flags, jobs,
            f"pass {number} propose",
        )
        final = finalize_skips(
            saved_ir, size, proposed, proven, opt_flags,
            f"pass {number} finalize",
        )
        log(f"pass {number}: proposed {len(proposed)}, finalized {len(final)}")
        if not final:
            break
        proven |= final
    return proven


def log(message):
    print(message, file=sys.stderr, flush=True)
