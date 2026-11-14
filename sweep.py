"""Full sweep with the gated design: one skip per instruction, then joint
rounds in which proven-dead instructions are nopped."""

import concurrent.futures
import collections
import re
import subprocess
import sys
import time

import stage2

start = time.time()
# gated anchors leave jump targets open until stage 2, which needs the
# inliner and jump threading of the standard pipelines to resolve them
saved = open("gen/gated.ll").read()
ops = subprocess.run(
    ["./sgpl_dce", "ops", "prog100.json"], capture_output=True, text=True
).stdout.splitlines()
n = len(ops)
# local anchors count: their switch gates their presence as a match target
real = [i for i, op in enumerate(ops) if op != "Global Anchor" and not op.startswith("Nop-")]

proven = set()


def prove(todo, pipeline):
    """Add to `proven` what can be shown dead on top of it: each position
    alone, then the candidates together, then the stragglers one by one."""
    def single(i):
        kept, _ = stage2.run(saved, {i}, pipeline, nopped=proven)
        return i not in kept

    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        remaining = {i for i, gone in zip(todo, pool.map(single, todo)) if gone}
    print(f"{pipeline}: dead when skipped alone: {len(remaining)} ({time.time() - start:.0f} s)", flush=True)
    # joint rounds: what vanishes together is proven; nop it and retry the rest
    for round_ in range(1, 8):
        kept, _ = stage2.run(saved, remaining, pipeline, nopped=proven)
        gone = remaining - kept
        print(f"{pipeline}: joint round {round_}: {len(gone)} proven, {len(remaining - gone)} left", flush=True)
        proven.update(gone)
        remaining -= gone
        if not gone or not remaining:
            break
    for i in sorted(remaining):
        kept, _ = stage2.run(saved, {i}, pipeline, nopped=proven)
        if i not in kept:
            proven.add(i)
            remaining.discard(i)
    print(f"{pipeline}: {len(proven)} proven, {len(remaining)} left ({time.time() - start:.0f} s)", flush=True)
    return remaining


# Pass 2 retries everything still unproven on top of pass 1: some positions
# are only dead once others are gone.
prove(real, "lite5")
remaining = prove([i for i in real if i not in proven], "lite5")
mask = "".join("1" if i in proven else "0" for i in range(n))
check = subprocess.run(
    ["./sgpl_dce", "validate", "prog100.json", mask, "1000", "300"],
    capture_output=True, text=True,
).stdout.strip().splitlines()[-1]
print("engine check:", check)
print(f"proven dead together: {len(proven)} of {len(real)} real ops; total {time.time() - start:.0f} s")
open("mask100_skip.txt", "w").write("".join("1" if i in proven else "0" for i in range(n)))
open("skip_leftover.txt", "w").write(",".join(map(str, sorted(remaining))))
hand = open("mask100.txt").read()
hand_real = {i for i in real if hand[i] == "1"}
print(f"hand-written mask on the same ops: {len(hand_real)}; shared {len(proven & hand_real)}, "
      f"only skip {len(proven - hand_real)}, only hand {len(hand_real - proven)}")
print("only skip:", collections.Counter(ops[i] for i in proven - hand_real).most_common(6))
print("only hand:", collections.Counter(ops[i] for i in hand_real - proven).most_common(6))
