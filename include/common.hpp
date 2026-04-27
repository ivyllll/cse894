// common.hpp - shared utilities: memory counters and cache-line alignment
#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>

namespace lf {

// Cache-line size. On Apple Silicon (M-series) the LLC line is 128B;
// most x86-64 systems use 64B.
#if defined(__APPLE__) && defined(__aarch64__)
constexpr size_t CACHE_LINE = 128;
#else
constexpr size_t CACHE_LINE = 64;
#endif

// Global memory-event counters used by the benchmark to measure
// reclamation behavior. Each counter sits on its own cache line to
// avoid false sharing across threads.
struct alignas(CACHE_LINE) AlignedCounter {
    std::atomic<int64_t> v{0};
    char pad[CACHE_LINE - sizeof(std::atomic<int64_t>)];
};

inline AlignedCounter g_allocated;
inline AlignedCounter g_freed;
inline AlignedCounter g_retired;

inline void on_alloc()  { g_allocated.v.fetch_add(1, std::memory_order_relaxed); }
inline void on_free()   { g_freed.v.fetch_add(1, std::memory_order_relaxed); }
inline void on_retire() { g_retired.v.fetch_add(1, std::memory_order_relaxed); }

inline int64_t live_objects() {
    // Number of allocated but not yet freed nodes (includes nodes that
    // have been retired but whose actual delete has not yet run).
    return g_allocated.v.load(std::memory_order_relaxed)
         - g_freed.v.load(std::memory_order_relaxed);
}

inline int64_t pending_objects() {
    // Number of retired but not yet freed objects. This is the
    // reclamation-lag metric reported in the paper.
    return g_retired.v.load(std::memory_order_relaxed)
         - g_freed.v.load(std::memory_order_relaxed);
}

inline void reset_counters() {
    g_allocated.v.store(0);
    g_freed.v.store(0);
    g_retired.v.store(0);
}

} // namespace lf
