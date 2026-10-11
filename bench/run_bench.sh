#!/usr/bin/env bash
# Usage: bench/run_bench.sh [program.mini ...]
# Markdown table of weighted dynamic cost: unoptimized vs fixed pipeline vs
# searched, plus search effort. Exits 1 if any optimized program differs
# from the original (VERIFY ... MISMATCH).
set -euo pipefail
BIN=${BIN:-./minicc}
if [ "$#" -gt 0 ]; then
  FILES=("$@")
else
  FILES=(samples/valid.mini samples/constant_folding.mini
         samples/loop_invariant.mini samples/loop_invariant_chain.mini
         samples/loop_no_invariant.mini samples/loop_with_if.mini
         samples/order_copyprop_fold.mini samples/order_strength_cse.mini
         samples/order_loop_mix.mini)
fi

"$BIN" --stats --search "${FILES[0]}" | tr -d '\r' | grep '^cost weights' || true
echo
echo "| program | unopt | fixed | searched | searched vs fixed | candidates | search ms |"
echo "|---|---:|---:|---:|---:|---:|---:|"

fail=0
for f in "${FILES[@]}"; do
  out=$("$BIN" --stats --search "$f" | tr -d '\r')
  col() { echo "$out" | awk -v a="$1" -v c="$2" '$1=="STAT" && $2==a {print $c}'; }
  unopt=$(col unoptimized 3)
  fixed=$(col fixed 3)
  searched=$(col searched 3)
  cand=$(col search_candidates 3)
  ms=$(col search_ms 3)
  if echo "$out" | grep -q MISMATCH; then
    echo "MISMATCH: optimized program differs from original: $f" >&2
    fail=1
  fi
  pct=$(awk -v f="$fixed" -v s="$searched" \
        'BEGIN { if (f > 0) printf "%.1f%%", (f - s) * 100.0 / f; else print "n/a" }')
  printf "| %s | %s | %s | %s | %s | %s | %s |\n" \
         "$(basename "$f")" "$unopt" "$fixed" "$searched" "$pct" "$cand" "$ms"
done
exit $fail
