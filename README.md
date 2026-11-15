# sgpl-static

[
![CI](https://github.com/mmore500/sgpl-static/actions/workflows/ci.yaml/badge.svg)
](https://github.com/mmore500/sgpl-static/actions)
[
![License](https://img.shields.io/github/license/mmore500/sgpl-static.svg)
](https://github.com/mmore500/sgpl-static/blob/main/LICENSE)
[
![GitHub stars](https://img.shields.io/github/stars/mmore500/sgpl-static.svg?style=round-square&logo=github&label=Stars&logoColor=white)](https://github.com/mmore500/sgpl-static)

> [!WARNING]
> sgpl-static is experimental. It may produce incorrect results, and its interface is subject to change.

sgpl-static strips dead instructions from a [signalgp-lite](https://github.com/mmore500/signalgp-lite) program, replacing them with nops.

- Free software: MIT
- Repository: <https://github.com/mmore500/sgpl-static>

An instruction is dead if it can never run, or if it runs but nothing it computes can ever reach anything observable.
Dead instructions are found with the clang/LLVM optimizer.
The program is translated to C++ that calls the project's real op functors in program order, and compiled to LLVM IR.
In that translation every instruction sits behind a *maybe skip*: a call the optimizer cannot see into decides whether the instruction runs.
The optimizer then does its ordinary dead-code elimination.
If it deletes an instruction's maybe-skip call, running or skipping that instruction makes no observable difference, so the instruction is dead.

Instructions are tested all at once and then, for those not yet found dead, one at a time (pass 1); those found dead are confirmed together and from then on always skipped.
The remaining instructions are tested again with those gone (pass 2), since removing one instruction can make another dead.
Passes repeat until one makes no further nops.

## Usage

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.3.0 \
  --input genome.json \
  --input-key value0.program \
  --project path/to/project \
  --peripheral peripheral.hpp \
  --cxxflags "-Iinclude -Ithird-party/signalgp-lite/include" \
  --solver opaque-skip \
  --output stripped.json
```

`--input` is a program or a genome that contains one; the output has the same shape.
`--input-key` says where the instruction list is in that JSON, as dot-separated keys.
`--project` is the code the program runs in, fetched with its submodules.
`--peripheral` says what is observable.
`--cxxflags` go to clang++, which runs from the project root; they must put signalgp-lite's `include` on the include path.

| Option | Meaning | Default |
|---|---|---|
| `--input` | program or genome JSON: file, URL, or `-` for stdin | `-` |
| `--input-key` | where the instruction list is, e.g. `value0.program` | required |
| `--project` | directory, tarball URL, or `git-url@ref` | required |
| `--peripheral` | peripheral header: file or URL | required |
| `--output` | stripped JSON: file, or `-` for stdout | `-` |
| `--cxxflags` | flags for clang++ to build naive IR, e.g. `"-Iinclude -std=c++20"` | none |
| `--optflags` | flags for clang optimizer to strip IR code | `-passes=default<O2>` |
| `--solver` | how dead instructions are found; only `opaque-skip` for now | required |
| `--max-passes` | stop after this many passes; `0` repeats until a pass makes no further nops | `0` |
| `--seed` | shuffle the order in which conflicting skips are retried | by position |
| `--jobs` | optimizer runs at once | `SLURM_CPUS_PER_TASK`, else all cores |

Suggested optimizer passes:

```bash
--optflags '-passes=function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce'
```

## Usage Example: DISHTINY

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.3.0 \
  --input https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/tests/data/dishtiny-prq49-series16005-stint100.json \
  --input-key value0.program \
  --project https://github.com/mmore500/dishtiny@978abb3e1839820b77693490d34d8ce88e4fba2d \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/peripherals/dishtiny/prq49-as-of-2026-11-14.hpp \
  --cxxflags "-Iinclude -Ithird-party/signalgp-lite/include" \
  --solver opaque-skip \
  --optflags '-passes=function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce' \
  --output stripped.json
```

## Usage Example: Symbulation

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.3.0 \
  --input https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/tests/data/symbulation-NotNandProgram100.json \
  --input-key value0 \
  --project https://github.com/anyaevostinar/SymbulationEmp@08a4cd4beddb52438fe77de749c7d2ff2b085e91 \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/peripherals/symbulation/main-as-of-2026-11-14.hpp \
  --cxxflags "-Isource -Isignalgp-lite/include -IEmpirical/include -std=c++20 -DUIT_VENDORIZE_EMP -DTAG_NUM_BITS=32" \
  --solver opaque-skip \
  --optflags '-passes=function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce' \
  --output stripped.json
```

## Installation

Run via apptainer (aka singularity):

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.3.0 --help
```

Apptainer install instructions for Linux, Mac, and Windows are [here](https://apptainer.org/docs/admin/main/installation.html).

## AI Use

Substantial portions of sgpl-static were prepared using agentic coding tools.
