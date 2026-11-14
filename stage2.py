"""Stage 2 of the gated design: set the skip switches in the saved IR and
re-optimize with `opt`. Returns the positions whose skip() call survives."""

import re
import subprocess
import sys
import time

DECL = re.compile(r"^@sgpl_enabled = .*$", re.M)
KEPT = re.compile(r"call[^\n]*@skip\(i32 noundef (\d+)\)")
CLEAN = (
    "function(sroa,early-cse,instcombine,simplifycfg,sccp,jump-threading,"
    "gvn,dse,adce,simplifycfg,instcombine)"
)
LITE = "function(sroa,early-cse,instcombine,simplifycfg,sccp,adce,simplifycfg)"
# Gated local anchors leave jump targets open until stage 2. This is the part
# of default<O2> that resolves them, found by deleting passes from O2 while it
# still removed everything O2 removes: inlining interleaved with cleanup,
# MemorySSA-based CSE, loop unrolling and deletion (the ops' "for each
# matching anchor" loops run 0 or 1 times), GVN and jump threading.
IC = "instcombine<no-verify-fixpoint>"
SC = "simplifycfg<switch-range-to-icmp>"
LITE5 = (
    f"function(early-cse,{IC}),"
    "cgscc(devirt<4>(inline,function<eager-inv;no-rerun>("
    f"sroa,early-cse<memssa>,{SC},loop(loop-unroll-full),sroa,gvn,bdce,"
    "jump-threading))),"
    "deadargelim,"
    f"function(loop(loop-deletion),{IC},simplifycfg<forward-switch-cond;"
    f"switch-range-to-icmp;hoist-common-insts;sink-common-insts>,{IC}),"
    "globaldce"
)
PIPELINES = {
    "lite5": LITE5,
    "lean2": f"ipsccp,globalopt,{CLEAN},deadargelim,{CLEAN},globaldce",
    "lite": f"ipsccp,globalopt,{LITE},deadargelim,{LITE},deadargelim,{LITE},globaldce",
    "lite2": f"ipsccp,globalopt,{LITE},deadargelim,{LITE},globaldce",
    "O1": "default<O1>",
    "O2": "default<O2>",
    "lean": f"ipsccp,globalopt,{CLEAN},deadargelim,{CLEAN},deadargelim,{CLEAN},globaldce",
}


def run(saved_ir, enabled, pipeline="lite2", nopped=()):
    """enabled: positions that may be skipped; nopped: positions always
    skipped. Everything else always runs."""
    nopped = set(nopped)
    table = "".join(
        "\\02" if i in nopped else "\\01" if i in enabled else "\\00"
        for i in range(4096)
    )
    text = DECL.sub(
        f'@sgpl_enabled = internal constant [4096 x i8] c"{table}", align 16',
        saved_ir,
    )
    start = time.time()
    out = subprocess.run(
        ["opt", "-S", f"-passes={PIPELINES[pipeline]}"],
        input=text, capture_output=True, text=True, check=True,
    ).stdout
    return set(map(int, KEPT.findall(out))), time.time() - start


if __name__ == "__main__":
    saved = open(sys.argv[1]).read()
    pick = list(map(int, open("pick20.txt").read().split()))
    hand = open("mask100.txt").read()
    for pipeline in sys.argv[2:]:
        wrong, total = [], 0.0
        for p in pick:
            kept, seconds = run(saved, {p}, pipeline)
            total += seconds
            if (p not in kept) != (hand[p] == "1"):
                wrong.append((p, "kept" if p in kept else "gone"))
        print(
            f"{pipeline}: {total / len(pick):.1f} s per variant, "
            f"agree {len(pick) - len(wrong)} of {len(pick)}, wrong: {wrong}",
            flush=True,
        )
