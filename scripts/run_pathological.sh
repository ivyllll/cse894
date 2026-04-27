#!/usr/bin/env bash
# run_pathological.sh - oversubscription scenario showing EBR's memory
# bloat under thread preemption.
#
# Modeling a "slow thread" via oversubscription (threads > physical cores)
# triggers frequent OS preemption. Under those conditions:
#   - EBR cannot advance its global epoch -> pending objects explode
#   - HP keeps pending objects bounded
# This is the highest-contrast comparison in the project.

set -e
cd "$(dirname "$0")/.."

OUT="results/pathological.csv"
mkdir -p results
./bench all 1 0.5 100 100 header > "$OUT"

# 16 threads on a typical 4-8 core laptop = severe oversubscription.
for nt in 1 2 4 8 16 32; do
    for v in HP EBR; do
        echo "[pathological] $v threads=$nt..."
        ./bench "$v" "$nt" 0.5 3000 1000 csv >> "$OUT"
    done
done

echo "Done. Data in $OUT"
column -s, -t < "$OUT"
