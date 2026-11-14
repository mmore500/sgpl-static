# sgpl dead-instruction prototype

Prototype tools for finding instructions in a signalgp-lite program that can be replaced by nops without changing observable behavior.
Everything here was run on one genome: dishtiny prq49, series 16005, stint 100 (`genome_stint100_raw.json`).

## Requirements

- clang 18 and LLVM 18 tools (`opt`), g++ 13, Python 3.
- signalgp-lite master checked out at `/tmp/sgpl/repo` with the conduit, Empirical, cereal, robin-hood-hashing and span-lite submodules.
  That path is hard-coded in the scripts and compile commands.
- Build the tool: `g++ -std=c++20 -O2 -DNDEBUG -w -I /tmp/sgpl/repo/include sgpl_dce.cpp -o sgpl_dce`

## Design: gated maybe-skip (`lower_fn.hpp`, `stage2.py`, `sweep.py`)

1. `sgpl_dce gencpp` writes the program as C++ that calls the real op functors.

   ```
   mkdir -p gen
   printf 'FILE gen/gated.cpp\nFN x -2 -\nEND\n' | ./sgpl_dce gencpp prog100.json
   ```
2. Stage 1: compile once with clang, every instruction's skip behind a switch (`gen/gated.ll`).

   ```
   clang++ -std=c++20 -DNDEBUG -w -fno-math-errno -fno-jump-tables -DSGPL_SKIP -DSGPL_GATED \
     -DSGPL_FLAT_STEPS -fignore-exceptions -I /tmp/sgpl/repo/include -I . -O2 -S -emit-llvm \
     gen/gated.cpp -o gen/gated.ll
   ```
3. Stage 2: `stage2.py` sets the switches in the saved IR and reruns `opt`.
   An instruction is dead if its `skip(N)` call is gone.
4. `sweep.py` tests every instruction alone, then proves the survivors together in joint rounds.
   Result: `mask100_skip.txt` (638 of 1,394 ops and local anchors; log in `sweep.log`).

### Local anchors

A local anchor has no code to skip; what it does is exist as a jump and regulator target.
Its switch therefore gates its presence in the module's local jump table:

- The generator passes each instruction's local match as a chain, best match first, for example `LPICK(16, LPICK(20, -1))`.
  The match is the first anchor in the chain that is present.
  This is exact because sgpl's selector takes the minimum score and the first-inserted anchor on ties.
- "The module has local anchors" and the table's identity token are expressions over the same switches.

With anchors gated, stage 1 can no longer resolve jump targets, so stage 2 has to.
The short pass list used before (`lite2`) cannot; `default<O2>` can, at about 2.5 s per variant.
`lite5` in `stage2.py` is the part of `default<O2>` that matters here, at about 1.1 s per variant.
It was found by deleting passes from `default<O2>` one at a time while the result still removed everything `default<O2>` removes.
What it adds over `lite2`: inlining interleaved with cleanup, MemorySSA-based CSE, loop unrolling and deletion, GVN and jump threading.

`sweep.py` runs two passes with `lite5`.
The second retries everything still unproven on top of the first, because some positions are only dead once others are gone.

## Files

| File | What it is |
|---|---|
| `sgpl_dce.cpp` | Generator and the real-engine differential validator |
| `mock_spec.hpp` | Spec with stand-ins for dishtiny's peripheral ops (same names and register use, mock state) |
| `lower_core.hpp` | Analysis-only `sgpl::Core` specialization used by generated programs |
| `lower_whole.hpp` | Shared declarations included by `lower_fn.hpp` |
| `lower_fn.hpp` | Macros for the module-per-function form, RNG patch, maybe-skip and gating |
| `stage2.py`, `sweep.py` | Stage 2 and the full sweep |
| `genome_stint100_raw.json` | Original genome |
| `prog100.json` | Just the program array, in the form `sgpl_dce` reads |
| `mask100.txt` | Earlier hand-written-lowering mask (623), used by `sweep.py` for comparison |
| `mask100_skip.txt` | Result of the sweep; one character per instruction, `1` = nop it |
| `skip_leftover.txt` | Positions dead when skipped alone but not proven together (none in the current run) |

## Results

| | Dead | Of |
|---|---|---|
| Ops | 559 | 1,228 |
| Local anchors | 79 | 166 |
| Total | 638 | 1,394 |

- The engine check passed: 1,000 randomized runs, no difference.
- The sweep took 714 s on 4 cores.
- Two passes of `default<O1>` then `default<O2>` give the identical mask, in about twice the time.
- Before anchors were gated, the op sweep proved 553 ops; all of them are still proven.
- The hand-written mask prunes 623 of the same positions: 619 are shared, 19 are only here, and 4 (all local anchors) only there.

## Caveats

- The dishtiny ops are mocks; real dishtiny was never run.
- signalgp-lite master was used, not the commit dishtiny pins.
- "Engine-checked" means 1,000 randomized runs in the real sgpl engine showed no difference; it is not a proof.
- Cores are assumed to start only at global anchors by tag match, with registers possibly seeded by the host.
