"""The sweep: find a set of positions that are dead together."""

import concurrent.futures
import sys

import tqdm

from .optimize import surviving_skips


def sweep(saved_ir, candidates, size, opt_flags, jobs, max_passes):
    """Returns the positions among `candidates` proven dead together.
    `max_passes`: 0 for as many passes as it takes."""
    proven = set()

    def run(maybe):
        return surviving_skips(saved_ir, size, maybe, proven, opt_flags)

    def prove(name, todo):
        """Add to `proven` what can be shown dead on top of it: each position
        alone, then the candidates together, then the stragglers one by one."""
        with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
            alone = pool.map(lambda i: i not in run({i}), todo)
            # a bar on stderr, redrawn at most every 20 seconds
            alone = tqdm.tqdm(alone, name, len(todo), mininterval=20)
            remaining = {i for gone, i in zip(alone, todo) if gone}
        log(f"dead when skipped alone: {len(remaining)}")
        # what vanishes together is proven; nop it and retry the rest
        while remaining:
            gone = remaining - run(remaining)
            proven.update(gone)
            remaining -= gone
            log(f"joint round: {len(gone)} proven, {len(remaining)} left")
            if not gone:
                break
        for i in sorted(remaining):
            if i not in run({i}):
                proven.add(i)
        log(f"proven: {len(proven)}")

    # Each pass retries everything still unproven on top of the passes before
    # it: some positions are only dead once others are gone. A pass that
    # proves nothing leaves nothing for the next one to build on.
    number = 0
    while not max_passes or number < max_passes:
        number += 1
        already = len(proven)
        prove(f"pass {number}", sorted(set(candidates) - proven))
        if len(proven) == already:
            break
    return proven


def log(message):
    print(message, file=sys.stderr, flush=True)
