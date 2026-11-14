"""Which skips hold when tried alone?"""

import concurrent.futures

import tqdm

from .optimize_skips import optimize_skips


def propose_skips(saved_ir, size, todo, proven, opt_flags, jobs, name):
    """The skips in `todo` that hold when tried alone, given that those in
    `proven` are made already. `name` labels the progress bar."""
    def holds(position):
        return bool(optimize_skips(saved_ir, size, {position}, proven, opt_flags))

    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        # a bar on stderr, redrawn at most every 20 seconds
        alone = tqdm.tqdm(pool.map(holds, todo), name, len(todo), mininterval=20)
        return {position for held, position in zip(alone, todo) if held}
