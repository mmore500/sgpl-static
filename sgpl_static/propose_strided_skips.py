"""Which skips hold when tried in spaced-out groups?"""

import concurrent.futures

import tqdm

from .optimize_skips import optimize_skips


def propose_strided_skips(
    saved_ir: str,
    size: int,
    todo: list[int],
    proven: set[int],
    opt_flags: list[str],
    jobs: int,
    name: str,
    strides: tuple[int, ...],
) -> set[int]:
    """The skips in `todo` that hold in some group, given that those in
    `proven` are made already. For each stride there is one group per
    remainder: the positions that leave that remainder when divided by the
    stride. A skip mostly depends on its neighbours, and a group has none of
    them, so a group finds nearly what trying its members alone would.
    `name` labels the progress bar."""
    groups = [
        {position for position in todo if position % stride == remainder}
        for stride in strides
        for remainder in range(stride)
    ]

    def held(group: set[int]) -> set[int]:
        return optimize_skips(saved_ir, size, group, proven, opt_flags)

    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        # a bar on stderr, redrawn at most every 20 seconds
        found = tqdm.tqdm(pool.map(held, groups), name, len(groups), mininterval=20)
        return set().union(*found)
