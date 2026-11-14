"""Which skips hold when tried alone?"""

import concurrent.futures

import tqdm

from .optimize_skips import optimize_skips


def propose_skips(
    saved_ir: str,
    size: int,
    todo: list[int],
    proven: set[int],
    opt_flags: list[str],
    jobs: int,
    name: str,
) -> set[int]:
    """The skips in `todo` that hold when tried alone, given that those in
    `proven` are made already. `name` labels the progress bar."""
    def holds(position: int) -> bool:
        return bool(optimize_skips(saved_ir, size, {position}, proven, opt_flags))

    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        # a bar on stderr, redrawn at most every 20 seconds
        alone = tqdm.tqdm(pool.map(holds, todo), name, len(todo), mininterval=20)
        return {position for held, position in zip(alone, todo) if held}
