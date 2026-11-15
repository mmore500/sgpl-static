"""One pass: propose skips, then finalize the proposals."""

import logging
import random
import typing


def do_pass(
    propose: typing.Callable[..., set[int]],
    finalize: typing.Callable[..., set[int]],
    saved_ir: str,
    size: int,
    todo: list[int],
    proven: set[int],
    opt_flags: list[str],
    jobs: int,
    number: int,
    rng: random.Random | None,
) -> set[int]:
    """The skips in `todo` that this pass proves, given that those in
    `proven` are made already. `propose` and `finalize` are the solver's two
    steps. `number` labels the pass; `rng` is passed to `finalize`."""
    proposed = propose(
        saved_ir, size, todo, proven, opt_flags, jobs, f"pass {number} propose"
    )
    final = finalize(
        saved_ir, size, proposed, proven, opt_flags,
        f"pass {number} finalize", rng,
    )
    logging.info(f"pass {number}: proposed {len(proposed)}, finalized {len(final)}")
    return final
