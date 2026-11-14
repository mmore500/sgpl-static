"""The sweep: find a set of positions that are dead together."""

import concurrent.futures
import sys

import tqdm

from . import stage2


def sweep(saved_ir, candidates, size, passes, jobs):
    """Returns the positions among `candidates` proven dead together."""
    proven = set()

    def run(maybe):
        return stage2.kept(saved_ir, size, maybe, proven, passes)

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

    # The second pass retries everything still unproven on top of the first:
    # some positions are only dead once others are gone.
    prove("pass 1", sorted(candidates))
    prove("pass 2", sorted(set(candidates) - proven))
    return proven


def log(message):
    print(message, file=sys.stderr, flush=True)
