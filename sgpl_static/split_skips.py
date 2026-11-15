"""Which proposed skips hold together? Settled by splitting and retrying."""

import random

import tqdm

from .optimize_skips import optimize_skips


def split_skips(
    saved_ir: str,
    size: int,
    proposed: set[int],
    proven: set[int],
    opt_flags: list[str],
    name: str,
    rng: random.Random | None,
) -> set[int]:
    """The skips in `proposed` that hold together, given that those in
    `proven` are made already. `name` labels the progress bar.

    A group is tried together and what holds is final. What is left is split
    in two by taking every other one, so each half stays spread out, and each
    half is retried on top of what is final so far. A skip that does not hold
    alone is dropped. Which of two conflicting skips wins depends on the
    order: by position, or shuffled by `rng` if it is not None."""
    final: set[int] = set()
    order = sorted(proposed)
    if rng is not None:
        rng.shuffle(order)
    # a bar on stderr counting proposals settled, redrawn at most every 20 s
    with tqdm.tqdm(
        desc=name, total=len(order), mininterval=20, disable=not order
    ) as settled:

        def settle(group: list[int]) -> None:
            if not group:
                return
            held = optimize_skips(
                saved_ir, size, set(group), proven | final, opt_flags
            )
            final.update(held)
            rest = [position for position in group if position not in held]
            if len(group) == 1:
                settled.update()
                return
            settled.update(len(held))
            settle(rest[0::2])
            settle(rest[1::2])

        settle(order)
    return final
