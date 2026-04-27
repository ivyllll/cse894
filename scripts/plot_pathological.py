#!/usr/bin/env python3
"""Plot the pathological-scenario figure (HP vs EBR under oversubscription)."""

import os
import pandas as pd
import matplotlib.pyplot as plt

OUT_DIR = "figures"
os.makedirs(OUT_DIR, exist_ok=True)

df = pd.read_csv("results/pathological.csv")

fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 4.5))

# Left panel: throughput
for v, color, marker in [("HP", "#1f77b4", "o"), ("EBR", "#2ca02c", "^")]:
    d = df[df.variant == v].sort_values("threads")
    ax1.plot(d.threads, d.throughput_mops, marker=marker, color=color,
             label=v, linewidth=2, markersize=9)
ax1.set_xlabel("Threads (physical cores = 2)")
ax1.set_ylabel("Throughput (Mops/sec)")
ax1.set_title("Throughput under oversubscription")
ax1.axvline(x=2, color='gray', linestyle='--', alpha=0.5, label='physical cores')
ax1.legend()
ax1.grid(True, alpha=0.3)

# Right panel: peak pending memory (log scale)
for v, color, marker in [("HP", "#1f77b4", "o"), ("EBR", "#2ca02c", "^")]:
    d = df[df.variant == v].sort_values("threads")
    ax2.plot(d.threads, d.peak_pending, marker=marker, color=color,
             label=v, linewidth=2, markersize=9)
ax2.set_xlabel("Threads (physical cores = 2)")
ax2.set_ylabel("Peak pending objects (log scale)")
ax2.set_title("Memory bloat under thread preemption")
ax2.set_yscale("log")
ax2.axvline(x=2, color='gray', linestyle='--', alpha=0.5)
ax2.legend()
ax2.grid(True, alpha=0.3, which='both')

plt.suptitle("Pathological Scenario: HP vs EBR under thread oversubscription",
             fontsize=12, y=1.02)
plt.tight_layout()
out = f"{OUT_DIR}/pathological.png"
plt.savefig(out, dpi=150, bbox_inches='tight')
print(f"saved {out}")

# Print summary
print("\nSummary:")
print(df[["variant","threads","throughput_mops","peak_pending"]].to_string(index=False))
