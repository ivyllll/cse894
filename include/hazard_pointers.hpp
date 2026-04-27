// hazard_pointers.hpp - Hazard Pointer infrastructure
//
// Reference: Maged M. Michael, "Hazard Pointers: Safe Memory Reclamation
//            for Lock-Free Objects", IEEE TPDS 2004.
//
// Core idea:
//   1. Every thread owns K hazard-pointer slots (K=2 here; the stack only
//      uses one).
//   2. Before dereferencing a shared pointer, a thread publishes it to one
//      of its hazard slots.
//   3. After publishing, the thread re-checks the shared pointer to ensure
//      it has not changed in the meantime (because another thread could
//      have unlinked and retired it before the publication became visible).
//   4. When a thread retires a node, it scans every other thread's hazard
//      slots. If no thread has the node listed as hazardous, it is safe to
//      free; otherwise it is deferred until the next scan.
//
// Pros: bounded reclamation latency -- at most O(threads * K) retired
//       objects can be pending at any time.
// Cons: each pop incurs at least two atomic stores to a hazard slot plus
//       a memory fence.

#pragma once

#include <atomic>
#include <vector>
#include <unordered_set>
#include <stdexcept>
#include <thread>
#include "common.hpp"

namespace lf {

class HPManager {
public:
    static constexpr int MAX_THREADS = 64;
    static constexpr int HP_PER_THREAD = 2;

private:
    struct alignas(CACHE_LINE) Slot {
        std::atomic<void*> hp[HP_PER_THREAD];
        std::atomic<bool> in_use{false};
        // Per-thread retired list. Thread-local in effect, so no locking
        // is needed when appending to it.
        std::vector<std::pair<void*, void(*)(void*)>> retired;
        char pad[CACHE_LINE]; // isolate from neighbors

        Slot() {
            for (int i = 0; i < HP_PER_THREAD; i++) hp[i].store(nullptr);
        }
    };

    Slot slots_[MAX_THREADS];

public:
    // Acquire a slot when a thread starts using HP.
    int acquire_slot() {
        for (int i = 0; i < MAX_THREADS; i++) {
            bool expected = false;
            if (slots_[i].in_use.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel)) {
                return i;
            }
        }
        throw std::runtime_error("HP slot exhausted");
    }

    // Release a slot when a thread exits, freeing what it can.
    void release_slot(int idx) {
        scan(idx);
        // Whatever could not be reclaimed is leaked (a simplification).
        // Negligible in long-running benchmarks.
        slots_[idx].retired.clear();
        for (int i = 0; i < HP_PER_THREAD; i++) {
            slots_[idx].hp[i].store(nullptr, std::memory_order_release);
        }
        slots_[idx].in_use.store(false, std::memory_order_release);
    }

    // Mark a pointer as hazardous (so other threads must not free it).
    // The store must be seq_cst so that the subsequent re-load of the
    // shared pointer observes any concurrent unlink.
    void protect(int my_slot, int hp_idx, void* p) {
        slots_[my_slot].hp[hp_idx].store(p, std::memory_order_seq_cst);
    }

    void clear(int my_slot, int hp_idx) {
        slots_[my_slot].hp[hp_idx].store(nullptr, std::memory_order_release);
    }

    // Append a node to this thread's retired list. Triggers a scan when
    // the list reaches the threshold R.
    void retire(int my_slot, void* p, void(*deleter)(void*)) {
        on_retire();
        slots_[my_slot].retired.emplace_back(p, deleter);
        // R = 2 * MAX_THREADS * HP_PER_THREAD per Michael's original paper.
        if (slots_[my_slot].retired.size() >=
            (size_t)(2 * MAX_THREADS * HP_PER_THREAD)) {
            scan(my_slot);
        }
    }

    // Scan all threads' hazard pointers, freeing retired nodes that are
    // not currently declared hazardous by any thread.
    void scan(int my_slot) {
        // Phase 1: collect every published hazard pointer.
        std::unordered_set<void*> hazards;
        hazards.reserve(MAX_THREADS * HP_PER_THREAD);
        for (int i = 0; i < MAX_THREADS; i++) {
            // Even if a slot is not in_use, read its hp anyway to be safe
            // against state-change races.
            for (int j = 0; j < HP_PER_THREAD; j++) {
                void* h = slots_[i].hp[j].load(std::memory_order_acquire);
                if (h) hazards.insert(h);
            }
        }

        // Phase 2: free retired nodes that are not hazardous.
        auto& retired = slots_[my_slot].retired;
        size_t write = 0;
        for (size_t read = 0; read < retired.size(); read++) {
            if (hazards.find(retired[read].first) == hazards.end()) {
                // Not hazardous: free it.
                retired[read].second(retired[read].first);
            } else {
                // Still hazardous: keep it for a later scan.
                retired[write++] = retired[read];
            }
        }
        retired.resize(write);
    }
};

// Singleton manager.
inline HPManager& hp_mgr() {
    static HPManager m;
    return m;
}

// RAII guard: each thread acquires its slot at construction and releases
// it at destruction.
class HPThread {
    int slot_;
public:
    HPThread() : slot_(hp_mgr().acquire_slot()) {}
    ~HPThread() { hp_mgr().release_slot(slot_); }
    int slot() const { return slot_; }

    HPThread(const HPThread&) = delete;
    HPThread& operator=(const HPThread&) = delete;
};

// Each thread has its own HPThread; first access initializes it.
inline HPThread& this_thread_hp() {
    thread_local HPThread t;
    return t;
}

} // namespace lf
