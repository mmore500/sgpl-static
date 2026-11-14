"""Stage 2: set the skip switches in the saved IR and re-optimize with `opt`.
A position is dead if its skip() call does not survive."""

import re
import subprocess

DECL = re.compile(r"^@sgpl_enabled = .*$", re.M)
KEPT = re.compile(r"call[^\n]*@skip\(i32 noundef (\d+)\)")

# The part of default<O2> this IR needs, found by deleting passes from O2
# while the result still removed everything O2 removes. Gated local anchors
# leave jump targets open until stage 2; resolving them takes inlining
# interleaved with cleanup, MemorySSA-based CSE, loop unrolling and deletion
# (the ops' "for each matching anchor" loops run 0 or 1 times), GVN and jump
# threading.
IC = "instcombine<no-verify-fixpoint>"
SC = "simplifycfg<switch-range-to-icmp>"
PIPELINES = {
    "lite5": (
        f"function(early-cse,{IC}),"
        "cgscc(devirt<4>(inline,function<eager-inv;no-rerun>("
        f"sroa,early-cse<memssa>,{SC},loop(loop-unroll-full),sroa,gvn,bdce,"
        "jump-threading))),"
        "deadargelim,"
        f"function(loop(loop-deletion),{IC},simplifycfg<forward-switch-cond;"
        f"switch-range-to-icmp;hoist-common-insts;sink-common-insts>,{IC}),"
        "globaldce"
    ),
    "O1": "default<O1>",
    "O2": "default<O2>",
    "O3": "default<O3>",
}


def kept(saved_ir, size, maybe, nopped, pipeline):
    """Positions whose skip() call survives. `maybe`: positions that may be
    skipped; `nopped`: positions always skipped; the rest always run.
    `pipeline`: a name in PIPELINES, or anything `opt -passes=` accepts."""
    table = "".join(
        "\\02" if i in nopped else "\\01" if i in maybe else "\\00"
        for i in range(size)
    )
    text = DECL.sub(
        f'@sgpl_enabled = internal constant [{size} x i8] c"{table}", align 16',
        saved_ir,
    )
    out = subprocess.run(
        ["opt", "-S", f"-passes={PIPELINES.get(pipeline, pipeline)}"],
        input=text, capture_output=True, text=True, check=True,
    ).stdout
    return set(map(int, KEPT.findall(out)))
