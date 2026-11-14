"""Which skips hold when tried all at once, or else alone?"""

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
    """The skips in `todo` that hold when all of `todo` is tried at once, or
    else when tried alone, given that those in `proven` are made already.
    `name` labels the progress bar."""
    together = optimize_skips(saved_ir, size, set(todo), proven, opt_flags)
    rest = [position for position in todo if position not in together]

    def holds(position: int) -> bool:
        return bool(optimize_skips(saved_ir, size, {position}, proven, opt_flags))

    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        # a bar on stderr, redrawn at most every 20 seconds
        alone = tqdm.tqdm(pool.map(holds, rest), name, len(rest), mininterval=20)
        return together | {
            position for held, position in zip(alone, rest) if held
        }
