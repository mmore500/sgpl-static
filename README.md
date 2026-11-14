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

Instructions are tested one at a time and then together: those proven dead are skipped outright and the rest are retried, since removing one instruction can make another dead.

## Usage

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.1.0 \
  --program genome.json \
  --program-key value0.program \
  --project path/to/project \
  -I include -I third-party/signalgp-lite/include \
  --peripheral peripheral.hpp \
  --output stripped.json
```

`--program` is a program or a genome that contains one; the output has the same shape.
`--program-key` says where the instruction list is in that JSON, as dot-separated keys.
`--project` is the code the program runs in, fetched with its submodules.
`-I` puts a directory of the project on the include path; signalgp-lite's `include` must be among them.
`--peripheral` says what is observable.

| Option | Meaning | Default |
|---|---|---|
| `--program` | program or genome JSON: file, URL, or `-` for stdin | `-` |
| `--program-key` | where the instruction list is, e.g. `value0.program` | required |
| `--project` | directory, tarball URL, or `git-url@ref` | required |
| `--peripheral` | peripheral header: file or URL | required |
| `--output` | stripped JSON: file, or `-` for stdout | `-` |
| `-I` | include directory, relative to the project root; repeatable | none |
| `-passes` | optimizer passes, as `opt -passes=` takes them | `default<O2>` |
| `--cxxflag` | extra compiler flag, e.g. `-std=c++20`; repeatable | none |
| `--jobs` | optimizer runs at once | `SLURM_CPUS_PER_TASK`, else all cores |
| `--workdir` | scratch directory, kept afterwards | a temporary directory |

Suggested optimizer passes:

```bash
-passes='function(early-cse,instcombine<no-verify-fixpoint>),cgscc(devirt<4>(inline,function<eager-inv;no-rerun>(sroa,early-cse<memssa>,simplifycfg<switch-range-to-icmp>,loop(loop-unroll-full),sroa,gvn,bdce,jump-threading))),deadargelim,function(loop(loop-deletion),instcombine<no-verify-fixpoint>,simplifycfg<forward-switch-cond;switch-range-to-icmp;hoist-common-insts;sink-common-insts>,instcombine<no-verify-fixpoint>),globaldce'
```

## Usage Example: DISHTINY

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.1.0 \
  --program https://raw.githubusercontent.com/mmore500/sgpl-static/1c67b75f8579a0bf87c82dac9e5f2b832fb038b5/examples/dishtiny.json \
  --program-key value0.program \
  --project https://github.com/mmore500/dishtiny@978abb3e1839820b77693490d34d8ce88e4fba2d \
  -I include -I third-party/signalgp-lite/include -I /opt/mpi-include \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/1c67b75f8579a0bf87c82dac9e5f2b832fb038b5/peripherals/dishtiny.hpp \
  --output stripped.json
```

conduit's headers include `<mpi.h>`; the image has MPI's headers at `/opt/mpi-include`.

The peripheral header, [peripherals/dishtiny.hpp](peripherals/dishtiny.hpp), defines `sgpl_static::spec_t`, the signalgp-lite spec to analyse.
DISHTINY's real ops run against it unchanged.
The header decides what is observable.
Values it passes to `sgpl_static::observe` are: an instruction that can change them is kept.

```cpp
// a message to another cell is observable: its tag and its registers
struct ObservedOutput {
  template<typename Message> bool TryPut(const Message& message) {
    observe(std::hash<dish_t::tag_t>{}(std::get<0>(message)));
    for (const float reg : std::get<1>(message)) observe(reg);
    return true;
  }
};
```

A function with an empty body has no effect, so an instruction that does nothing else is removed.

```cpp
// the self-send buffers are not observable: what is sent there is dropped
struct Discarded {
  template<typename Message> void push_back(const Message&) {}
};
```

To change what counts as observable, change a member's type in the header's `Peripheral`.
The helpers are in [include/sgpl_static/shim.hpp](include/sgpl_static/shim.hpp).

## Usage Example: Symbulation

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.1.0 \
  --program https://raw.githubusercontent.com/mmore500/sgpl-static/1c67b75f8579a0bf87c82dac9e5f2b832fb038b5/examples/symbulation.json \
  --program-key value0 \
  --project https://github.com/anyaevostinar/SymbulationEmp@08a4cd4beddb52438fe77de749c7d2ff2b085e91 \
  -I source -I signalgp-lite/include -I Empirical/include \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/1c67b75f8579a0bf87c82dac9e5f2b832fb038b5/peripherals/symbulation.hpp \
  --cxxflag=-std=c++20 --cxxflag=-DUIT_VENDORIZE_EMP --cxxflag=-DTAG_NUM_BITS=32 \
  --output stripped.json
```

Symbulation's instructions talk to a `CPUState`.
The peripheral header, [peripherals/symbulation.hpp](peripherals/symbulation.hpp), supplies one for a stand-in world.
Outputs, reproducing, donating, stealing and infecting are observable.

```cpp
template<typename... Org> void SymDonateToHost(Org&&...) { observe(DONATE); }
```

## Installation

Run via apptainer (aka singularity):

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:v0.1.0 --help
```

## AI Use

Substantial portions of sgpl-static were prepared using agentic coding tools.
