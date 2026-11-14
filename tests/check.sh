#!/bin/bash
# Strip a program, then compare original and stripped in the real engine.
# usage: tests/check.sh <harness> <sgpl-static arguments...>
# env EXPECT_MASK: file with the mask the strip must reproduce
set -euo pipefail
cd "$(dirname "$0")/.."
harness=$1; shift
work=$(mktemp -d)
# the reduced pass list from the README: much quicker than the default
python3 -m sgpl_static "$@" --pipeline "$(cat tests/lite5.txt)" \
  --workdir "$work" --output "$work/out.json"
if [ -n "${EXPECT_MASK:-}" ]; then
  cmp "$work/mask.txt" "$EXPECT_MASK"
fi
cp "tests/harness/$harness.hpp" "$work/harness.hpp"
clang++ $(cat "$work/flags") -DNDEBUG -w -O2 tests/validate.cpp -o "$work/validate"
"$work/validate" "$work/program.json" "$work/stripped.json" 1000 300
