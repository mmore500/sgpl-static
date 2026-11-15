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

The objective of this project is to reduce the workload of "nopout" experiments by using clang's optimizer for static analysis to preemptively strip provably dead code out of evolved programs (i.e.,  unreachable or provably inconsequential instructions).

## Implementation

Behind the scenes, the input signalgp-lite program is translated to C++ source calling the program's op functors in instruction order, and compiled to LLVM IR.
Dead instructions are then found using the LLVM optimizer.

Every instruction is gated by an opaque "maybe skip" variable.
If the optimizer elides an instruction's "maybe skip" check, running or skipping that instruction makes no observable difference.
Thus, the instruction can be considered dead and safely replaced with nop.

Instructions are tested all at once and then, for those not yet found dead, individually (pass 1).
Instructions found dead are pooled together for confirmation, and then permanently eliminated.
Remaining instructions may then be tested again (pass 2), since removing instructions can make others dead.
Passes repeat until no further nops are found, or maximum passes is reached.

The `--solver` kwarg selects among available screening procedures.
Unlike single-instruction tests by the naive `opaque-skip` solver, the `stride711-split` solver adds instructions in strided sets (every 7th, then every 11th).
Each pass pools and tests newly proposed skips together, with any unaccepted skips recursively split and retried.
This solver substantially reduces necessary optimizer runs, while producing comparable results.

## Usage

```bash
singularity run --cleanenv docker://ghcr.io/mmore500/sgpl-static:v0.3.2 \
  --input genome.json \
  --input-key value0.program \
  --project path/to/project \
  --peripheral peripheral.hpp \
  --cxxflags "-Iinclude -Ithird-party/signalgp-lite/include" \
  --solver stride711-split \
  --output stripped.json
```

| Option | Meaning | Default |
|---|---|---|
| `--input` | program or genome JSON: file, URL, or `-` for stdin | `-` |
| `--input-key` | path to instruction list in input JSON schema, e.g. `value0.program` | required |
| `--project` | directory, tarball URL, or `git-url@ref` | required |
| `--peripheral` | peripheral header: file or URL | required |
| `--output` | stripped JSON: file, or `-` for stdout | `-` |
| `--cxxflags` | flags for clang++ to build naive IR, e.g. `"-Iinclude -std=c++20"` | none |
| `--optflags` | flags for clang optimizer to strip IR code | `-passes=default<O2>` |
| `--solver` | how dead instructions are found: `opaque-skip` or `stride711-split` | required |
| `--max-passes` | stop after this many passes; `0` repeats until a pass makes no further nops | `0` |
| `--seed` | shuffle the order in which conflicting skips are retried | none |
| `--jobs` | how many optimizer trials to dispatch in parallel | `SLURM_CPUS_PER_TASK`, else all cores |

Suggested effective but fast-to-evaluate subset of `-O2` optimizer passes:

```bash
--optflags '-passes=function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce'
```

## Usage Example: DISHTINY

```bash
singularity run --cleanenv docker://ghcr.io/mmore500/sgpl-static:v0.3.2 \
  --input https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/tests/data/dishtiny-prq49-series16005-stint100.json \
  --input-key value0.program \
  --project https://github.com/mmore500/dishtiny@978abb3e1839820b77693490d34d8ce88e4fba2d \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/peripherals/dishtiny/prq49-as-of-2026-11-14.hpp \
  --cxxflags "-Iinclude -Ithird-party/signalgp-lite/include" \
  --solver stride711-split \
  --optflags '-passes=function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce' \
  --output /dev/stdout
```

## Usage Example: Symbulation

```bash
singularity run --cleanenv docker://ghcr.io/mmore500/sgpl-static:v0.3.2 \
  --input https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/tests/data/symbulation-NotNandProgram100.json \
  --input-key value0 \
  --project https://github.com/anyaevostinar/SymbulationEmp@08a4cd4beddb52438fe77de749c7d2ff2b085e91 \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/380be308d15554931d4dcfa476022eff300573ee/peripherals/symbulation/main-as-of-2026-11-14.hpp \
  --cxxflags "-Isource -Isignalgp-lite/include -IEmpirical/include -std=c++20 -DUIT_VENDORIZE_EMP -DTAG_NUM_BITS=32" \
  --solver stride711-split \
  --optflags '-passes=function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce' \
  --output /dev/stdout
```

## Installation

Run via singularity (aka apptainer):

```bash
singularity run --cleanenv docker://ghcr.io/mmore500/sgpl-static:v0.3.2 --help
```

Apptainer install instructions for Linux, Mac, and Windows are [here](https://apptainer.org/docs/admin/main/installation.html).

## AI Use

Substantial portions of sgpl-static were prepared using agentic coding tools.
