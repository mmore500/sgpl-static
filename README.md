# sgpl-static

[
![CI](https://github.com/mmore500/sgpl-static/actions/workflows/ci.yaml/badge.svg)
](https://github.com/mmore500/sgpl-static/actions)
[
![GitHub stars](https://img.shields.io/github/stars/mmore500/sgpl-static.svg?style=round-square&logo=github&label=Stars&logoColor=white)](https://github.com/mmore500/sgpl-static)

> [!WARNING]
> sgpl-static is experimental. It may produce incorrect results, and its interface is subject to change.

sgpl-static strips a [signalgp-lite](https://github.com/mmore500/signalgp-lite) program: it replaces instructions that cannot affect anything observable with nops. JSON in, JSON out.

- Repository: <https://github.com/mmore500/sgpl-static>

The program is written out as C++ that calls the project's real op functors, and compiled to LLVM IR once.
Each instruction sits behind an opaque "maybe skip" choice.
If the optimizer can delete that choice, skipping the instruction changes nothing observable, so the instruction is dead.
What counts as observable is set by a *peripheral header*: a small C++ file that stands in for whatever the ops talk to.

Nopped instructions keep their position and their RNG draws, so program length, timing and the random stream are unchanged.
Global anchors are never touched.
The result is a set proven dead together, not the largest possible one.

## Usage

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:latest \
  --program https://raw.githubusercontent.com/mmore500/sgpl-static/main/examples/dishtiny.json \
  --project https://github.com/mmore500/dishtiny@prq49 \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/main/peripherals/dishtiny.hpp \
  --output stripped.json
```

`--program` is a program or a genome that contains one; the output has the same shape.
`--project` is the code the program runs in, fetched with its submodules; its root and its signalgp-lite go on the include path.
`--peripheral` says what is observable.
Each takes a local path or a URL.

For Symbulation, which needs a few compiler settings:

```bash
apptainer run docker://ghcr.io/mmore500/sgpl-static:latest \
  --program https://raw.githubusercontent.com/mmore500/sgpl-static/main/examples/symbulation.json \
  --project https://github.com/anyaevostinar/SymbulationEmp@main \
  --include Empirical/include \
  --peripheral https://raw.githubusercontent.com/mmore500/sgpl-static/main/peripherals/symbulation.hpp \
  --std c++20 --cxxflag=-DUIT_VENDORIZE_EMP --cxxflag=-DTAG_NUM_BITS=32 \
  --output stripped.json
```

| Option | Meaning | Default |
|---|---|---|
| `--program` | program or genome JSON: file, URL, or `-` for stdin | required |
| `--project` | directory, tarball URL, or `git-url@ref` | required |
| `--peripheral` | peripheral header: file or URL | required |
| `--output` | stripped JSON: file, or `-` for stdout | `-` |
| `--include` | further include directory, relative to the project root; repeatable | none |
| `--pipeline` | stage-2 passes: `lite5`, `O1`, `O2`, `O3`, or anything `opt -passes=` accepts | `lite5` |
| `--std` | C++ standard | `c++17` |
| `--cxxflag` | extra compiler flag; repeatable | none |
| `--jobs` | parallel `opt` runs | `SLURM_CPUS_PER_TASK`, else all cores |
| `--report` | write a JSON report: mask, counts, time | none |
| `--workdir` | scratch directory, kept afterwards | a temporary directory |

Git checkouts are cached by commit under `~/.cache/sgpl-static` (or `$XDG_CACHE_HOME`).
Given a full commit hash, a cached project needs no network.

A stronger `--pipeline` can prove more instructions dead, never wrong ones; `lite5` is the part of `O2` that mattered on the dishtiny example, at under half the time.
A peripheral header fetched by URL is compiled and run, so pin it to a commit.

## Peripheral headers

A peripheral header defines `sgpl_static::spec_t`, the signalgp-lite spec to analyse.
The project's real ops run against it unchanged.
Each thing an op can do to the peripheral is one of two kinds:

```cpp
// observable: every instruction feeding this call is kept
struct ObservedOutput {
  template<typename Message> bool TryPut(const Message& message) {
    sgpl_static::observe(std::get<0>(message)); // ...
    return true;
  }
};

// a no-op: instructions that only feed it are removed
struct Discarded {
  template<typename Message> bool TryPut(const Message&) { return true; }
};
```

To declare something a no-op, give it an empty body.
Start from [peripherals/dishtiny.hpp](peripherals/dishtiny.hpp) or [peripherals/symbulation.hpp](peripherals/symbulation.hpp).

Ops that set the program counter themselves, as Symbulation's jumps do, are supported in programs without local anchors.
The header can announce their targets with `jump_dests`; see the Symbulation header.

## Installation

The tool is the container image; there is nothing else to install.

```bash
apptainer pull sgpl-static.sif docker://ghcr.io/mmore500/sgpl-static:latest
```

The image's filesystem is read-only under Apptainer, so scratch files go to `--workdir` or `$TMPDIR`.
On compute nodes without network access, pass `--project`, `--program` and `--peripheral` as local paths.

## Assumptions

- Cores start only at global anchors, with registers the host may have set.
- A global jump may land on any global anchor; global regulator writes and fork requests are always observable.
- Local regulators are private to a core. After a core writes one, its later local jumps may land on any local anchor of the module.
- Jump tables pick one match: the best score, first-inserted on ties (signalgp-lite's default selector).
- The peripheral's own state may change between any two instructions.

## Testing

CI strips each example in the container, then runs original and stripped side by side in the real signalgp-lite engine on 1,000 randomized runs and compares everything observable ([tests/check.sh](tests/check.sh)).
That check is evidence, not proof, and it is not part of the container.

## AI Use

Substantial portions of sgpl-static were prepared using agentic coding tools.
