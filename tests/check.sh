#!/bin/bash
# Strip a program, then compare original and stripped in the real engine.
# usage: tests/check.sh <harness> <sgpl-static arguments...>
# env EXPECT_MASK: file with the mask the strip must reproduce
# env DEFAULT_PIPELINE: if set, use the default --pipeline
set -euo pipefail
cd "$(dirname "$0")/.."
harness=$1; shift
work=$(mktemp -d)
# -P: import the installed copy (PYTHONPATH), not this checkout
export PYTHONPATH="${PYTHONPATH:-$PWD}"
# the reduced pass list from the README is much quicker than the default
pipeline=(--pipeline "$(cat tests/lite5.txt)")
if [ -n "${DEFAULT_PIPELINE:-}" ]; then pipeline=(); fi
SGPL_STATIC_MASK_DUMP="$work/mask.txt" python3 -P -m sgpl_static "$@" \
  "${pipeline[@]}" --workdir "$work" --output "$work/out.json"
if [ -n "${EXPECT_MASK:-}" ]; then
  cmp "$work/mask.txt" "$EXPECT_MASK"
fi
cp "tests/harness/$harness.hpp" "$work/harness.hpp"
clang++ $(cat "$work/flags") -DNDEBUG -w -O2 tests/validate.cpp -o "$work/validate"
"$work/validate" "$work/program.json" "$work/stripped.json" 1000 300
