"""Strip dead instructions from a signalgp-lite program, replacing them with
nops."""

import argparse
import json
import os
import pathlib
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

from . import __version__
from .sweep import log, sweep

ROOT = pathlib.Path(__file__).resolve().parent.parent
CACHE = pathlib.Path(
    os.environ.get("XDG_CACHE_HOME") or pathlib.Path.home() / ".cache"
) / "sgpl-static"
STAGE1_FLAGS = [
    "-DNDEBUG", "-w", "-O2", "-fno-math-errno", "-fno-jump-tables",
    "-fignore-exceptions", "-S", "-emit-llvm",
]


def is_url(text):
    return text.startswith(("http://", "https://"))


def read(location):
    """Text of a local file or URL; `-` is stdin."""
    if location == "-":
        return sys.stdin.read()
    if is_url(location):
        with urllib.request.urlopen(location) as response:
            return response.read().decode()
    return pathlib.Path(location).read_text()


def fetch_git(url, ref):
    """A checkout of `url` at `ref` with its submodules, cached by commit."""
    if not re.fullmatch(r"[0-9a-f]{40}", ref):
        listed = subprocess.run(
            ["git", "ls-remote", url, ref or "HEAD"],
            check=True, capture_output=True, text=True,
        ).stdout.split()
        if not listed:
            sys.exit(f"sgpl-static: {url} has no branch or tag {ref!r}; "
                     "for a commit, give the full hash")
        ref = listed[0]
    cached = CACHE / f"{pathlib.PurePosixPath(url).name}-{ref}"
    if cached.exists():
        return cached
    CACHE.mkdir(parents=True, exist_ok=True)
    checkout = tempfile.mkdtemp(dir=CACHE)
    git = ["git", "-C", checkout]
    try:
        for command in (
            ["init", "-q"],
            ["fetch", "-q", "--depth", "1", url, ref],
            ["checkout", "-q", "FETCH_HEAD"],
            ["submodule", "update", "-q", "--init", "--recursive",
             "--depth", "1"],
        ):
            subprocess.run(git + command, check=True)
        os.rename(checkout, cached)
    except BaseException:
        shutil.rmtree(checkout)
        if not cached.exists():  # else another run cached it first
            raise
    return cached


def fetch_source(location, into):
    """A source tree: a local directory, a tarball URL, or `git-url@ref`."""
    if not is_url(location):
        return pathlib.Path(location).resolve()
    if location.endswith((".tar.gz", ".tgz")):
        shutil.rmtree(into, ignore_errors=True)
        into.mkdir()
        archive, _ = urllib.request.urlretrieve(location)
        with tarfile.open(archive) as tar:
            tar.extractall(into, filter="data")
        entries = list(into.iterdir())
        return entries[0] if len(entries) == 1 else into
    return fetch_git(*location.partition("@")[::2])


def find_program(document, key_path):
    """The instruction list at `key_path` (dot-separated keys) in a program
    or genome document, as (container, key) so it can be replaced in place."""
    holder, key = {"": document}, ""
    for step in filter(None, key_path.split(".")):
        if not isinstance(holder[key], dict) or step not in holder[key]:
            sys.exit(f"sgpl-static: no {step!r} at {key_path!r} in the input")
        holder, key = holder[key], step
    if not isinstance(holder[key], list):
        sys.exit(f"sgpl-static: {key_path!r} in the input is not a list")
    return holder, key


def main():
    log("warning: sgpl-static is experimental, it may produce incorrect results "
        "and its interface is subject to change")
    parser = argparse.ArgumentParser(prog="sgpl-static", description=__doc__)
    parser.add_argument("--program", default="-",
                        help="program or genome JSON: file, URL, or - for "
                        "stdin (default)")
    parser.add_argument("--program-key", required=True, metavar="PATH",
                        help="where the instruction list is in the JSON, as "
                        "dot-separated keys, e.g. value0.program")
    parser.add_argument("--output", default="-",
                        help="stripped JSON: file, or - for stdout (default)")
    parser.add_argument("--project", required=True,
                        help="project the program runs in, e.g. dishtiny: "
                        "directory, tarball URL, or git-url@ref")
    parser.add_argument("--peripheral", required=True,
                        help="peripheral header: file or URL")
    parser.add_argument("--cxxflags", default="",
                        help="flags for clang++, run from the project root: "
                        "e.g. '-Iinclude -std=c++20'")
    parser.add_argument("--optflags", default="-passes=default<O2>",
                        help="flags for opt (default: -passes=default<O2>)")
    parser.add_argument("--jobs", type=int,
                        default=int(os.environ.get("SLURM_CPUS_PER_TASK", 0))
                        or os.cpu_count(),
                        help="parallel opt runs (default: all cores)")
    parser.add_argument("--version", action="version", version=__version__)
    # argparse takes a value that starts with a dash for another option,
    # unless it is attached with "="
    argv = sys.argv[1:]
    for i, argument in enumerate(argv[:-1]):
        if argument in ("--cxxflags", "--optflags"):
            argv[i:i + 2] = [f"{argument}={argv[i + 1]}", ""]
    args = parser.parse_args(list(filter(None, argv)))

    document = json.loads(read(args.program))
    holder, key = find_program(document, args.program_key)
    with tempfile.TemporaryDirectory(prefix="sgpl-static-") as work:
        strip(args, holder, key, pathlib.Path(work))

    text = json.dumps(document, indent=4) + "\n"
    if args.output == "-":
        sys.stdout.write(text)
    else:
        pathlib.Path(args.output).write_text(text)


def strip(args, holder, key, work):
    """Nop the dead instructions of the program at holder[key], in place."""
    size = len(holder[key])
    (work / "peripheral.hpp").write_text(read(args.peripheral))
    (work / "program.json").write_text(json.dumps({"value0": holder[key]}))
    project = fetch_source(args.project, work / "project")
    flags = [f"-I{work}", f"-I{ROOT}/include", *shlex.split(args.cxxflags)]

    def run(*command, **kwargs):
        return subprocess.run(
            list(map(str, command)), check=True, text=True, **kwargs
        )

    def gen(*command):
        return run(work / "sgpl_gen", *command, stdout=subprocess.PIPE).stdout

    log("building the front end")
    # the user's flags are relative to the project root
    run("clang++", *flags, "-DNDEBUG", "-w", "-O1", ROOT / "source/sgpl_gen.cpp",
        "-o", work / "sgpl_gen", cwd=project)
    kinds = [line[0] for line in gen("ops", work / "program.json").splitlines()]
    gen("gencpp", work / "program.json", work / "program.cpp")
    log("stage 1: compiling the program")
    run("clang++", *flags, *STAGE1_FLAGS, work / "program.cpp",
        "-o", work / "program.ll", cwd=project)

    # ops and local anchors can go; global anchors and nops stay as they are
    candidates = [i for i, kind in enumerate(kinds) if kind in "OL"]
    log(f"stage 2: sweeping {len(candidates)} of {size} instructions")
    proven = sweep(
        (work / "program.ll").read_text(), candidates, size,
        shlex.split(args.optflags), args.jobs,
    )
    mask = "".join("1" if i in proven else "0" for i in range(size))
    stripped = gen("apply", work / "program.json", mask)
    holder[key] = json.loads(stripped)["value0"]
    log(f"nopped {len(proven)} of {len(candidates)} candidates")


if __name__ == "__main__":
    main()
