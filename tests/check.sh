#!/bin/bash
# Strip a program, then compare original and stripped in the real engine.
# usage: tests/check.sh <harness> <input> <input-key> <project>
#          <peripheral> <cxxflags> [<optflags>]
# env EXPECT_OUTPUT: file the stripped JSON must match exactly
set -euo pipefail
cd "$(dirname "$0")/.."
harness=$1 program=$2 key=$3 project=$4 peripheral=$5 cxxflags=$6
optflags=(${7:+--optflags "$7"})
work=$(mktemp -d)
# -P: import the installed copy (PYTHONPATH), not this checkout
export PYTHONPATH="${PYTHONPATH:-$PWD}"
python3 -P -m sgpl_static --input "$program" --input-key "$key" \
  --project "$project" --peripheral "$peripheral" --cxxflags "$cxxflags" \
  --solver opaque-skip \
  "${optflags[@]}" --output "$work/out.json"

if [ -n "${EXPECT_OUTPUT:-}" ]; then
  cmp "$work/out.json" "$EXPECT_OUTPUT"
fi

# the two instruction lists for the validator, and the project's root
root=$(python3 -P - "$program" "$work/out.json" "$key" "$work" "$project" <<'PY'
import json, pathlib, sys
from sgpl_static.__main__ import fetch_source, find_program
*documents, key, work, project = sys.argv[1:]
for name, document in zip(("original", "stripped"), documents):
    holder, last = find_program(json.load(open(document)), key)
    pathlib.Path(work, f"{name}.json").write_text(json.dumps({"value0": holder[last]}))
print(fetch_source(project, pathlib.Path(work, "project")))
PY
)

# the validator, compiled from the project root as the tool compiles
cp "$peripheral" "$work/peripheral.hpp"
cp "tests/harness/$harness.hpp" "$work/harness.hpp"
(cd "$root" && clang++ -I"$work" -I"$OLDPWD/include" $cxxflags -DNDEBUG -w -O2 \
  "$OLDPWD/tests/validate.cpp" -o "$work/validate")
"$work/validate" "$work/original.json" "$work/stripped.json" 1000 300
