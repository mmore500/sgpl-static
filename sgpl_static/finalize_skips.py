"""Which proposed skips hold together?"""

import tqdm

from .optimize_skips import optimize_skips


def finalize_skips(saved_ir, size, proposed, proven, opt_flags, name):
    """The skips in `proposed` that hold together, given that those in
    `proven` are made already. Proposals can conflict: two skips that each
    hold alone need not hold at once. `name` labels the progress bar."""
    final, left = set(), set(proposed)
    # a bar on stderr counting proposals settled, redrawn at most every 20 s
    with tqdm.tqdm(
        desc=name, total=len(left), mininterval=20, disable=not left
    ) as settled:
        # what holds together is final; the rest are retried on top of it
        while left:
            held = optimize_skips(saved_ir, size, left, proven | final, opt_flags)
            final |= held
            left -= held
            settled.update(len(held))
            if not held:
                break
        for position in sorted(left):  # stragglers, one by one
            final |= optimize_skips(
                saved_ir, size, {position}, proven | final, opt_flags
            )
            settled.update()
    return final
