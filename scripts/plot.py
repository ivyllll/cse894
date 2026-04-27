#!/usr/bin/env python3
"""
plot.py - generate the main figures from results/results.csv

Usage:
    python3 scripts/plot.py [results.csv]

Output: PNG files under figures/
Dependencies: matplotlib, pandas (pip install matplotlib pandas)
"""

import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

CSV_PATH = sys.argv[1] if len(sys.argv) > 1 else "results/results.csv"
OUT_DIR = "figures"
os.makedirs(OUT_DIR, exist_ok=True)

# Consistent palette and markers across figures.
COLORS = {"Leak": "#888888", "RC": "#d62728", "HP": "#1f77b4", "EBR": "#2ca02c"}
MARKERS = {"Leak": "x", "RC": "s", "HP": "o", "EBR": "^"}

df = pd.read_csv(CSV_PATH)
print(f"Loaded {len(df)} rows")
print(df.head())

# ============================================================
# Figure 1: throughput vs threads (fixed push_ratio)
# ============================================================
def plot_throughput_scaling(push_ratio=0.5):
    sub = df[df.push_ratio == push_ratio]
    if sub.empty:
        print(f"Skipping throughput plot: no data for push_ratio={push_ratio}")
        return
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for v in ["Leak", "RC", "HP", "EBR"]:
        d = sub[sub.variant == v].sort_values("threads")
        if d.empty:
            continue
        ax.plot(d.threads, d.throughput_mops,
                marker=MARKERS[v], color=COLORS[v], label=v,
                linewidth=2, markersize=8)
    ax.set_xlabel("Threads")
    ax.set_ylabel("Throughput (Mops/sec)")
    ax.set_title(f"Throughput vs Threads (push_ratio={push_ratio})")
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    out = f"{OUT_DIR}/throughput_threads_pr{push_ratio}.png"
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  saved {out}")

# ============================================================
# Figure 2: latency percentiles (fixed thread count)
# ============================================================
def plot_latency_percentiles(threads=4, push_ratio=0.5):
    sub = df[(df.threads == threads) & (df.push_ratio == push_ratio)]
    if sub.empty:
        print(f"Skipping latency plot: no data for threads={threads} push_ratio={push_ratio}")
        return
    variants = [v for v in ["Leak", "RC", "HP", "EBR"]
                if not sub[sub.variant == v].empty]
    percentiles = ["p50_ns", "p99_ns", "p999_ns"]
    labels = ["P50", "P99", "P99.9"]

    fig, ax = plt.subplots(figsize=(7.5, 4.5))
    x = list(range(len(variants)))
    width = 0.25
    for i, (p, lbl) in enumerate(zip(percentiles, labels)):
        vals = [sub[sub.variant == v][p].values[0] for v in variants]
        ax.bar([xi + (i-1)*width for xi in x], vals,
               width=width, label=lbl)
    ax.set_xticks(x)
    ax.set_xticklabels(variants)
    ax.set_ylabel("Latency (ns)")
    ax.set_yscale("log")
    ax.set_title(f"Latency percentiles (threads={threads}, push_ratio={push_ratio})")
    ax.legend()
    ax.grid(True, axis="y", alpha=0.3)
    plt.tight_layout()
    out = f"{OUT_DIR}/latency_percentiles_t{threads}.png"
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  saved {out}")

# ============================================================
# Figure 3: peak pending objects vs threads
#           (the figure most clearly distinguishing HP from EBR)
# ============================================================
def plot_pending_memory(push_ratio=0.5):
    sub = df[df.push_ratio == push_ratio]
    if sub.empty:
        return
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for v in ["RC", "HP", "EBR"]:  # Leak is uninteresting here
        d = sub[sub.variant == v].sort_values("threads")
        if d.empty:
            continue
        ax.plot(d.threads, d.peak_pending,
                marker=MARKERS[v], color=COLORS[v], label=v,
                linewidth=2, markersize=8)
    ax.set_xlabel("Threads")
    ax.set_ylabel("Peak Pending Objects (lower = better reclamation)")
    ax.set_title("Memory Reclamation Latency: Pending objects vs Threads")
    ax.set_yscale("symlog")
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    out = f"{OUT_DIR}/pending_memory.png"
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  saved {out}")

# ============================================================
# Figure 4: throughput vs workload mix (fixed thread count)
# ============================================================
def plot_workload_mix(threads=4):
    sub = df[df.threads == threads]
    if sub.empty:
        return
    ratios = sorted(sub.push_ratio.unique())
    if len(ratios) < 2:
        print(f"Skipping workload-mix plot: only one push_ratio")
        return
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for v in ["Leak", "RC", "HP", "EBR"]:
        d = sub[sub.variant == v].sort_values("push_ratio")
        if d.empty:
            continue
        ax.plot(d.push_ratio, d.throughput_mops,
                marker=MARKERS[v], color=COLORS[v], label=v,
                linewidth=2, markersize=8)
    ax.set_xlabel("Push ratio (1.0 = all push, 0.0 = all pop)")
    ax.set_ylabel("Throughput (Mops/sec)")
    ax.set_title(f"Throughput vs Workload mix (threads={threads})")
    ax.legend()
    ax.grid(True, alpha=0.3)
    plt.tight_layout()
    out = f"{OUT_DIR}/workload_mix_t{threads}.png"
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  saved {out}")

# ============================================================
# Main
# ============================================================
print("\nGenerating plots...")
for pr in sorted(df.push_ratio.unique()):
    plot_throughput_scaling(pr)
    plot_pending_memory(pr) if pr == 0.5 else None
for nt in sorted(df.threads.unique()):
    if nt >= 2:
        plot_latency_percentiles(nt)
        plot_workload_mix(nt)

# Text summary table
print("\n=== Summary ===")
summary = df.groupby(["variant", "threads"]).agg(
    {"throughput_mops": "mean",
     "p99_ns": "mean",
     "peak_pending": "max"}).reset_index()
print(summary.to_string(index=False))

print(f"\nAll figures saved under {OUT_DIR}/")
