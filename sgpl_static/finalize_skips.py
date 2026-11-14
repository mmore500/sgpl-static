"""Which proposed skips hold together?"""

from .optimize_skips import optimize_skips


def finalize_skips(saved_ir, size, proposed, proven, opt_flags):
    """The skips in `proposed` that hold together, given that those in
    `proven` are made already. Proposals can conflict: two skips that each
    hold alone need not hold at once."""
    final, left = set(), set(proposed)
    # what holds together is final; the rest are retried on top of it
    while left:
        held = optimize_skips(saved_ir, size, left, proven | final, opt_flags)
        final |= held
        left -= held
        if not held:
            break
    for position in sorted(left):  # stragglers, one by one
        final |= optimize_skips(
            saved_ir, size, {position}, proven | final, opt_flags
        )
    return final
