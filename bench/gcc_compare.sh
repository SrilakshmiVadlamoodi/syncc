#!/usr/bin/env bash
# OPTIONAL reference table. Compares the static instruction count of minicc's
# searched output against the static instruction count of the equivalent C
# function compiled by gcc -O0 / -O1. NOT a like-for-like comparison: see
# the caveats printed below and in REPORT.md.
set -euo pipefail
BIN=${BIN:-./minicc}
if ! command -v gcc >/dev/null 2>&1; then
  echo "gcc not found on PATH; skipping GCC side table" >&2
  exit 0
fi

# gcc output asm for `kernel`: count instruction lines (tab + lowercase letter),
# excluding directives (tab + '.') and labels.
count_insns() {
  gcc "$2" -S -o - "$1" | tr -d '\r' | awk '
    /^kernel:/ { f = 1; next }
    f && /\.seh_endproc|\.size[ \t]+kernel/ { f = 0 }
    f && /^\t[a-z]/ { n++ }
    END { print n + 0 }'
}

echo "| program | minicc searched (static ops) | gcc -O0 insns | gcc -O1 insns |"
echo "|---|---:|---:|---:|"
for c in bench/c/*.c; do
  name=$(basename "$c" .c)
  mini="samples/$name.mini"
  [ -f "$mini" ] || continue
  searched=$("$BIN" --stats --search "$mini" | tr -d '\r' \
             | awk '$1=="STAT" && $2=="searched" {print $4}')
  [ -z "$searched" ] && searched=$("$BIN" --stats "$mini" | tr -d '\r' \
             | awk '$1=="STAT" && $2=="fixed" {print $4}')
  printf "| %s | %s | %s | %s |\n" "$name" "$searched" \
         "$(count_insns "$c" -O0)" "$(count_insns "$c" -O1)"
done

cat <<'CAVEATS'

Caveats (indicative only):
- Units differ: TAC operations (no registers, no addressing modes) vs. x86-64
  instructions, whole program vs. the `kernel` function. Initial assignments
  that the C version receives as parameters are counted in the TAC.
- The C versions take their inputs from argv so GCC cannot evaluate the closed
  program at compile time; the mini programs use literal inputs.
- GCC at -O1 and above is expected to win. This table positions the project;
  it does not claim superiority.
CAVEATS
