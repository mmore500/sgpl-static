#!/bin/bash
# Strip a program, then compare original and stripped in the real engine.
# usage: tests/check.sh <harness> <program> <program-key> <project>
#          <peripheral> <cxxflags>
# env EXPECT_OUTPUT: file the stripped JSON must match exactly
# env DEFAULT_PASSES: if set, use the default --optflags
set -euo pipefail
cd "$(dirname "$0")/.."
harness=$1 program=$2 key=$3 project=$4 peripheral=$5 cxxflags=$6
work=$(mktemp -d)
# -P: import the installed copy (PYTHONPATH), not this checkout
export PYTHONPATH="${PYTHONPATH:-$PWD}"
# the suggested passes from the README are much quicker than the default
passes=(--optflags "-passes=$(cat tests/passes.txt)")
if [ -n "${DEFAULT_PASSES:-}" ]; then passes=(); fi
python3 -P -m sgpl_static --program "$program" --program-key "$key" \
  --project "$project" --peripheral "$peripheral" --cxxflags "$cxxflags" \
  "${passes[@]}" --output "$work/out.json"

if [ -n "${EXPECT_OUTPUT:-}" ]; then
  cmp "$work/out.json" "$EXPECT_OUTPUT"
fi

# the two instruction lists, for the validator
python3 -P - "$program" "$work/out.json" "$key" "$work" <<'PY'
import json, pathlib, sys
from sgpl_static.__main__ import find_program
*documents, key, work = sys.argv[1:]
for name, document in zip(("original", "stripped"), documents):
    holder, last = find_program(json.load(open(document)), key)
    pathlib.Path(work, f"{name}.json").write_text(json.dumps({"value0": holder[last]}))
PY

# the validator, compiled from the project root as the tool compiles
read -r root include < <(python3 -P -c '
import pathlib, sys
from sgpl_static.__main__ import ROOT, fetch_source
print(fetch_source(sys.argv[1], pathlib.Path(sys.argv[2])), ROOT / "include")
' "$project" "$work/project")
cp "$peripheral" "$work/peripheral.hpp"
cp "tests/harness/$harness.hpp" "$work/harness.hpp"
(cd "$root" && clang++ -I"$work" -I"$include" $cxxflags -DNDEBUG -w -O2 \
  "$OLDPWD/tests/validate.cpp" -o "$work/validate")
"$work/validate" "$work/original.json" "$work/stripped.json" 1000 300
