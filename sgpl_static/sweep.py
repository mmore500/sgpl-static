"""The sweep: find a set of positions that are dead together."""

import concurrent.futures
import sys

import tqdm

from .prove_skips import prove_skips


def sweep(saved_ir, candidates, size, opt_flags, jobs, max_passes):
    """Returns the positions among `candidates` proven dead together.
    `max_passes`: 0 for as many passes as it takes."""
    proven = set()

    def prove(maybe):
        return prove_skips(saved_ir, size, maybe, proven, opt_flags)

    def propose_skips(name, todo):
        """The skips in `todo` that are proven when tried alone, on top of
        what is proven already."""
        with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
            alone = pool.map(lambda i: bool(prove({i})), todo)
            # a bar on stderr, redrawn at most every 20 seconds
            alone = tqdm.tqdm(alone, name, len(todo), mininterval=20)
            proposed = {i for gone, i in zip(alone, todo) if gone}
        log(f"{name}: proposed {len(proposed)}")
        return proposed

    def confirm_skips(name, proposed):
        """Add to `proven` the proposals that are proven together; the rest
        are retried on top of those."""
        while proposed:
            gone = prove(proposed)
            proven.update(gone)
            proposed -= gone
            if not gone:
                break
        for i in sorted(proposed):  # stragglers, one by one
            proven.update(prove({i}))
        log(f"{name}: confirmed {len(proven)} in all")

    # Each pass retries everything still unproven on top of the passes before
    # it: some positions are only dead once others are gone. A pass that
    # proves nothing leaves nothing for the next one to build on.
    number = 0
    while not max_passes or number < max_passes:
        number += 1
        already = len(proven)
        name = f"pass {number}"
        confirm_skips(name, propose_skips(name, sorted(set(candidates) - proven)))
        if len(proven) == already:
            break
    return proven


def log(message):
    print(message, file=sys.stderr, flush=True)
