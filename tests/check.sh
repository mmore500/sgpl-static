#!/bin/bash
# Strip a program, then compare original and stripped in the real engine.
# usage: tests/check.sh <harness> <sgpl-static arguments...>
# env EXPECT_MASK: file with the mask the strip must reproduce
set -euo pipefail
cd "$(dirname "$0")/.."
harness=$1; shift
work=$(mktemp -d)
python3 -m sgpl_static "$@" --workdir "$work" --output "$work/out.json" \
  --report "$work/report.json"
grep -v '"mask"' "$work/report.json"
if [ -n "${EXPECT_MASK:-}" ]; then
  grep -q "\"$(cat "$EXPECT_MASK")\"" "$work/report.json"
fi
cp "tests/harness/$harness.hpp" "$work/harness.hpp"
clang++ $(cat "$work/flags") -DNDEBUG -w -O2 tests/validate.cpp -o "$work/validate"
"$work/validate" "$work/program.json" "$work/stripped.json" 1000 300
