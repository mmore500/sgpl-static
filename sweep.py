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
# inliner and jump threading of the standard pipeline to resolve them
PIPELINE = "O1"
saved = open("gen/gated.ll").read()
ops = subprocess.run(
    ["./sgpl_dce", "ops", "prog100.json"], capture_output=True, text=True
).stdout.splitlines()
n = len(ops)
# local anchors count: their switch gates their presence as a match target
real = [i for i, op in enumerate(ops) if op != "Global Anchor" and not op.startswith("Nop-")]

# stage 1 already ran with every skip open: a skip missing there is dead
open_everywhere = set(map(int, stage2.KEPT.findall(saved)))
dead_all_at_once = [i for i in real if i not in open_everywhere]
todo = [i for i in real if i in open_everywhere]
print(f"real ops: {len(real)}; dead with all skips open: {len(dead_all_at_once)}", flush=True)

def single(i):
    kept, _ = stage2.run(saved, {i}, PIPELINE)
    return i not in kept

with concurrent.futures.ThreadPoolExecutor(4) as pool:
    alone = [i for i, gone in zip(todo, pool.map(single, todo)) if gone]
candidates = set(dead_all_at_once) | set(alone)
print(f"dead when skipped alone: {len(candidates)} ({time.time() - start:.0f} s)", flush=True)

# joint rounds: what vanishes together is proven; nop it and retry the rest
proven, remaining = set(), set(candidates)
for round_ in range(1, 8):
    kept, _ = stage2.run(saved, remaining, PIPELINE, nopped=proven)
    gone = remaining - kept
    print(f"joint round {round_}: {len(gone)} proven, {len(remaining - gone)} left", flush=True)
    if not gone:
        break
    proven |= gone
    remaining -= gone
    if not remaining:
        break
# leftovers: one at a time on top of everything proven so far
for i in sorted(remaining):
    kept, _ = stage2.run(saved, {i}, PIPELINE, nopped=proven)
    if i not in kept:
        proven.add(i)
        remaining.discard(i)
print(f"after one-at-a-time pass over leftovers: {len(proven)} proven, {len(remaining)} left")
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
