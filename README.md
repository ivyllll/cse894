# Lock-free Stack: A Comparative Study of Memory Reclamation Schemes

This project implements a Treiber stack with four different memory reclamation schemes and benchmarks them against each other to study how lock-free data structures can safely free deprecated nodes — the central correctness challenge in lock-free programming.

This is the source repository for the CSE 894 (Advanced Algorithms) final project.

## Reclamation Schemes Compared

| Scheme | File | Notes |
|--------|------|-------|
| Leak | `include/stack_leak.hpp` | Never frees nodes (performance baseline) |
| Reference Counting | `include/stack_rc.hpp` | Uses `std::shared_ptr` atomic operations |
| Hazard Pointers | `include/stack_hp.hpp` + `hazard_pointers.hpp` | Maged Michael's classic protocol (TPDS 2004) |
| Epoch-Based Reclamation | `include/stack_ebr.hpp` + `epoch.hpp` | Fraser's classic protocol; widely used in production (Folly, crossbeam) |

## Building

### Option 1: Direct compilation (simplest)

```bash
g++ -std=c++17 -O3 -Iinclude src/main.cpp -o bench -pthread
# On macOS, clang works equally well:
# clang++ -std=c++17 -O3 -Iinclude src/main.cpp -o bench
```

### Option 2: CMake

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
# Executable will be at build/bench
```

### Sanitizer builds (for concurrency debugging)

```bash
cd build && make bench_tsan    # ThreadSanitizer
cd build && make bench_asan    # AddressSanitizer
```

## Running

### Single run

```bash
./bench <variant> <threads> <push_ratio> <duration_ms> <prefill> [csv|header]

# Example: 8 threads, 50/50 push/pop, 3 seconds, 1000-element prefill, all variants
./bench all 8 0.5 3000 1000 csv

# Example: HP only, 4 threads, 90% push, 5 seconds, no prefill
./bench HP 4 0.9 5000 0 csv
```

### Full experimental sweep

```bash
# Full mode: takes about 10-15 minutes
bash scripts/run_all.sh

# Quick mode: takes about 2-3 minutes
bash scripts/run_all.sh quick

# Pathological scenario (oversubscription test for EBR memory bloat)
bash scripts/run_pathological.sh
```

### Generating figures

```bash
pip install matplotlib pandas
python3 scripts/plot.py
python3 scripts/plot_pathological.py
# Output: figures/*.png
```

## Repository Layout

```
.
├── include/                 # All header-only implementations
│   ├── common.hpp               # Memory counters, cache-line alignment
│   ├── stack_leak.hpp           # Leak baseline
│   ├── stack_rc.hpp             # Reference counting
│   ├── stack_hp.hpp             # Hazard-pointer stack
│   ├── stack_ebr.hpp            # EBR stack
│   ├── hazard_pointers.hpp      # HP infrastructure
│   └── epoch.hpp                # EBR infrastructure
├── src/main.cpp             # Benchmark driver
├── scripts/
│   ├── run_all.sh               # Parameter sweep
│   ├── run_pathological.sh      # Oversubscription scenario
│   ├── plot.py                  # Main plots
│   └── plot_pathological.py     # Pathological-scenario plots
├── results/                 # CSV outputs (committed; reproducible by scripts)
├── figures/                 # PNG figures (committed; reproducible by scripts)
└── CMakeLists.txt
```

## Metrics

Each benchmark run produces a CSV row with these fields:

- `throughput_mops` — Total throughput in millions of operations per second
- `avg_lat_ns / p50_ns / p99_ns / p999_ns` — Per-operation latency distribution (sampled at 1/64 to bound memory)
- `peak_live` — Peak number of simultaneously live `Node` objects
- `peak_pending` — Peak number of objects retired but not yet freed (i.e. reclamation lag) — **the key metric for distinguishing HP from EBR**

## Key References

- Maged M. Michael, *Hazard Pointers: Safe Memory Reclamation for Lock-Free Objects*, IEEE TPDS, 2004.
- Keir Fraser, *Practical Lock-Freedom*, PhD thesis, University of Cambridge, 2004.
- Anthony Williams, *C++ Concurrency in Action*, 2nd ed., Manning 2019. (Chapter 7)
- Tom Hart et al., *Performance of memory reclamation for lockless synchronization*, JPDC 2007.
- Implementation references: [Facebook Folly](https://github.com/facebook/folly), [libcds](https://github.com/khizmax/libcds), [crossbeam-epoch](https://docs.rs/crossbeam-epoch).

## Known Limitations

1. The RC implementation uses `std::atomic_load(shared_ptr*)`. On both libstdc++ and libc++, this falls back to an internal spinlock pool, so the RC variant is not truly lock-free in practice. This itself is one of the project's findings.
2. The EBR implementation is a simplified pedagogical version: it does not handle objects retired by departing threads beyond a best-effort cleanup, and does not implement the production optimizations (pinning timeouts, hybrid HP/EBR fallback) discussed in the final report.
3. The hazard-pointer slot table has a fixed upper bound of `MAX_THREADS = 64`, sufficient for course-scale experiments.
