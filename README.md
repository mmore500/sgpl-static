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

sgpl-static strips dead instructions from a [signalgp-lite](https://github.com/mmore500/signalgp-lite) program, replacing them with nops. An instruction is dead if it is unreachable or faint: nothing it computes ever reaches anything observable.

- Free software: MIT
- Repository: <https://github.com/mmore500/sgpl-static>

Dead instructions are found with the clang/LLVM optimizer.
The program is translated to C++ that calls the project's real op functors in program order, and compiled to LLVM IR.
In that translation every instruction sits behind a *maybe skip*: a call the optimizer cannot see into decides whether the instruction runs.
The optimizer then does its ordinary dead-code elimination.
If it deletes an instruction's maybe-skip call, running or skipping that instruction makes no observable difference, so the instruction is dead.
Instructions are tested one at a time and then together: those proven dead are skipped outright and the rest are retried, since removing one instruction can make another dead.
What the optimizer treats as observable comes from a *peripheral header*: C++ that stands in for everything the ops read and write outside the program.

## Usage

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.1.0 \
  --program https://raw.githubusercontent.com/mmore500/sgpl-static/main/examples/dishtiny.json \
  --program-key value0.program \
  --project https://github.com/mmore500/dishtiny@prq49 \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/main/peripherals/dishtiny.hpp \
  --output stripped.json
```

`--program` is a program or a genome that contains one; the output has the same shape.
`--program-key` says where the instruction list is in that JSON, as dot-separated keys.
`--project` is the code the program runs in, fetched with its submodules; its root and its signalgp-lite go on the include path.
`--peripheral` says what is observable.

| Option | Meaning | Default |
|---|---|---|
| `--program` | program or genome JSON: file, URL, or `-` for stdin | `-` |
| `--program-key` | where the instruction list is, e.g. `value0.program` | required |
| `--project` | directory, tarball URL, or `git-url@ref` | required |
| `--peripheral` | peripheral header: file or URL | required |
| `--output` | stripped JSON: file, or `-` for stdout | `-` |
| `--include` | further include directory, relative to the project root; repeatable | none |
| `--pipeline` | optimizer passes, as `opt -passes=` takes them | `default<O2>` |
| `--cxxflag` | extra compiler flag, e.g. `-std=c++20`; repeatable | none |
| `--jobs` | optimizer runs at once | `SLURM_CPUS_PER_TASK`, else all cores |
| `--workdir` | scratch directory, kept afterwards | a temporary directory |

Set `SGPL_STATIC_MASK_DUMP` to a path to also write the mask there: one character per instruction, `1` where it was nopped.

This shorter pass list is the part of `default<O2>` that mattered on the dishtiny example, and takes under half the time:

```bash
--pipeline 'function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce'
```

## Usage Example

Symbulation's instructions talk to a `CPUState`, and need a few compiler settings:

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.1.0 \
  --program https://raw.githubusercontent.com/mmore500/sgpl-static/main/examples/symbulation.json \
  --program-key value0 \
  --project https://github.com/anyaevostinar/SymbulationEmp@main \
  --include Empirical/include \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/main/peripherals/symbulation.hpp \
  --cxxflag=-std=c++20 --cxxflag=-DUIT_VENDORIZE_EMP --cxxflag=-DTAG_NUM_BITS=32 \
  --output stripped.json
```

The peripheral header, [peripherals/symbulation.hpp](peripherals/symbulation.hpp), defines `sgpl_static::spec_t`, the signalgp-lite spec to analyse.
Symbulation's real instructions run against it unchanged.
Each thing an instruction can do to the peripheral is one of two kinds:

```cpp
// observable: every instruction feeding this call is kept
template<typename... Org> void SymDonateToHost(Org&&...) { observe(DONATE); }

// a no-op: instructions that only feed it are removed
template<typename... Org> void SymDonateToHost(Org&&...) {}
```

The header as shipped treats donating as observable, the first form.
To declare something a no-op, give it an empty body.
The helpers are in [include/sgpl_static/shim.hpp](include/sgpl_static/shim.hpp).
For dishtiny, start from [peripherals/dishtiny.hpp](peripherals/dishtiny.hpp).

Symbulation's jump instructions set the program counter themselves.
That is supported in programs without local anchors, and the header announces the targets with `jump_dests`.

## Installation

The tool is the container image; there is nothing else to install.

```bash
apptainer pull sgpl-static.sif docker://ghcr.io/mmore500/sgpl-static:v0.1.0
```

The image's filesystem is read-only under Apptainer, so scratch files go to `--workdir` or `$TMPDIR`.
On compute nodes without network access, pass `--project`, `--program` and `--peripheral` as local paths.

## AI Use

Substantial portions of sgpl-static were prepared using agentic coding tools.
