#!/usr/bin/env bash
# run_all.sh - sweep parameter space and produce a complete CSV.
#
# Output: results/results.csv
# Runtime: about 5-15 minutes depending on hardware.
#
# Usage: bash scripts/run_all.sh [quick|full]
#        Defaults to "full".

set -e
cd "$(dirname "$0")/.."

MODE="${1:-full}"
OUT="results/results.csv"
mkdir -p results

if [[ "$MODE" == "quick" ]]; then
    THREADS=(1 2 4)
    RATIOS=(0.5)
    DURATION=1500
    PREFILLS=(1000)
else
    THREADS=(1 2 4 8)
    RATIOS=(0.1 0.5 0.9)        # pop-heavy / balanced / push-heavy
    DURATION=3000               # 3 seconds per configuration
    PREFILLS=(1000)
fi

VARIANTS=(Leak RC HP EBR)

# Write CSV header.
./bench all 1 0.5 100 100 header > "$OUT"

TOTAL=$(( ${#THREADS[@]} * ${#RATIOS[@]} * ${#PREFILLS[@]} * ${#VARIANTS[@]} ))
DONE=0

for nt in "${THREADS[@]}"; do
  for pr in "${RATIOS[@]}"; do
    for pf in "${PREFILLS[@]}"; do
      for v in "${VARIANTS[@]}"; do
        DONE=$((DONE+1))
        echo "[$DONE/$TOTAL] variant=$v threads=$nt push_ratio=$pr prefill=$pf ..."
        ./bench "$v" "$nt" "$pr" "$DURATION" "$pf" csv >> "$OUT"
      done
    done
  done
done

echo ""
echo "Done. Results written to $OUT"
echo "Preview:"
column -s, -t < "$OUT" | head -20
