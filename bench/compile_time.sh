#!/usr/bin/env bash
# Usage: bench/compile_time.sh [N ...]   (generated-program sizes, default: 500 1000 2000 4000)
# Prints per-stage compile time in ms for generated programs of N top-level
# statements, with and without --search. Sample programs compile in
# microseconds, so scaling is measured on generated inputs.
set -euo pipefail
BIN=${BIN:-./minicc}
SIZES=("$@")
[ ${#SIZES[@]} -eq 0 ] && SIZES=(500 1000 2000 4000)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

printf "%-8s %-9s %9s %9s %9s %9s %9s %10s\n" N mode lex parse codegen fold licm search
for n in "${SIZES[@]}"; do
  "$BIN" --gen "$n" --seed 1 > "$TMP/big.mini"
  for mode in fixed search; do
    flags="--time"
    [ "$mode" = search ] && flags="--time --search --budget 300"
    out=$("$BIN" $flags "$TMP/big.mini" | tr -d '\r')
    t() { echo "$out" | awk -v k="$1" '$1=="TIME" && $2==k {print $3}'; }
    printf "%-8s %-9s %9s %9s %9s %9s %9s %10s\n" "$n" "$mode" \
      "$(t lex)" "$(t parse)" "$(t codegen)" "$(t fold)" "$(t licm)" "$(t search)"
  done
done
