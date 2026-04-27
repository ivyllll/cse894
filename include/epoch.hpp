// epoch.hpp - Epoch-Based Reclamation (EBR) infrastructure
//
// References: Keir Fraser, "Practical Lock-Freedom", PhD thesis,
//             University of Cambridge, 2004; subsequent refinements by
//             Hart, McKenney et al. (RCU has the same flavor).
//
// Core idea:
//   1. Maintain a global "epoch" counter cycling through {0, 1, 2}.
//   2. When a thread enters a critical section, it copies the global epoch
//      into its local slot.
//   3. When retiring an object, place it into the bucket for the current
//      epoch.
//   4. If every active thread's local epoch is at least the current global
//      epoch, advance the global epoch by one. The bucket for
//      (current - 2) mod 3 is then safe to free, because no thread can
//      still be in that older epoch.
//
// Pros: enter/exit is just a single atomic store -- cheaper than HP.
// Cons: a thread that sleeps inside a critical section blocks all global
//       epoch advancement, causing pending memory to grow without bound.

#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include "common.hpp"

namespace lf {

class EBRManager {
public:
    static constexpr int MAX_THREADS = 64;
    static constexpr uint64_t INACTIVE = UINT64_MAX;

private:
    struct alignas(CACHE_LINE) Slot {
        std::atomic<uint64_t> local_epoch{INACTIVE};
        std::atomic<bool> in_use{false};
        // Three retired buckets, one per epoch in the rotation.
        std::vector<std::pair<void*, void(*)(void*)>> retired[3];
        char pad[CACHE_LINE];
    };

    alignas(CACHE_LINE) std::atomic<uint64_t> global_epoch_{0};
    Slot slots_[MAX_THREADS];

public:
    int acquire_slot() {
        for (int i = 0; i < MAX_THREADS; i++) {
            bool expected = false;
            if (slots_[i].in_use.compare_exchange_strong(expected, true,
                    std::memory_order_acq_rel)) {
                return i;
            }
        }
        throw std::runtime_error("EBR slot exhausted");
    }

    void release_slot(int idx) {
        // Best-effort cleanup of remaining retired objects on exit.
        for (int b = 0; b < 3; b++) {
            for (auto& [p, del] : slots_[idx].retired[b]) {
                del(p);
            }
            slots_[idx].retired[b].clear();
        }
        slots_[idx].local_epoch.store(INACTIVE, std::memory_order_release);
        slots_[idx].in_use.store(false, std::memory_order_release);
    }

    // Enter a critical section: copy global epoch into local slot.
    // Must be seq_cst so the subsequent shared-pointer reload sees the
    // hazard installation.
    void enter(int my_slot) {
        uint64_t e = global_epoch_.load(std::memory_order_acquire);
        slots_[my_slot].local_epoch.store(e, std::memory_order_seq_cst);
    }

    // Exit the critical section.
    void exit(int my_slot) {
        slots_[my_slot].local_epoch.store(INACTIVE, std::memory_order_release);
    }

    void retire(int my_slot, void* p, void(*deleter)(void*)) {
        on_retire();
        uint64_t e = slots_[my_slot].local_epoch.load(std::memory_order_relaxed);
        // We are inside a critical section, so e != INACTIVE.
        slots_[my_slot].retired[e % 3].emplace_back(p, deleter);

        // Periodically attempt to advance the global epoch.
        static thread_local int counter = 0;
        if (++counter % 64 == 0) {
            try_advance(my_slot);
        }
    }

    // Try to advance global_epoch from cur to cur+1.
    void try_advance(int my_slot) {
        uint64_t cur = global_epoch_.load(std::memory_order_acquire);

        // Check that every active thread is at cur (or INACTIVE).
        for (int i = 0; i < MAX_THREADS; i++) {
            if (!slots_[i].in_use.load(std::memory_order_acquire)) continue;
            uint64_t le = slots_[i].local_epoch.load(std::memory_order_acquire);
            if (le != INACTIVE && le != cur) {
                return; // a thread is lagging; cannot advance
            }
        }

        // All caught up; try to advance.
        uint64_t next = cur + 1;
        if (!global_epoch_.compare_exchange_strong(cur, next,
                std::memory_order_acq_rel)) {
            return; // someone else advanced first
        }

        // Advancement succeeded. Bucket[(cur-1) mod 3] (== bucket[(next+1) mod 3])
        // is now safe to free: it holds objects retired two epochs ago.
        int free_bucket = (next + 1) % 3;
        auto& bucket = slots_[my_slot].retired[free_bucket];
        for (auto& [p, del] : bucket) {
            del(p);
        }
        bucket.clear();
    }
};

inline EBRManager& ebr_mgr() {
    static EBRManager m;
    return m;
}

class EBRThread {
    int slot_;
public:
    EBRThread() : slot_(ebr_mgr().acquire_slot()) {}
    ~EBRThread() { ebr_mgr().release_slot(slot_); }
    int slot() const { return slot_; }

    EBRThread(const EBRThread&) = delete;
    EBRThread& operator=(const EBRThread&) = delete;
};

inline EBRThread& this_thread_ebr() {
    thread_local EBRThread t;
    return t;
}

// RAII guard: enter the epoch critical section in scope, exit on destruction.
class EBRGuard {
    int slot_;
public:
    EBRGuard() : slot_(this_thread_ebr().slot()) {
        ebr_mgr().enter(slot_);
    }
    ~EBRGuard() {
        ebr_mgr().exit(slot_);
    }
    int slot() const { return slot_; }

    EBRGuard(const EBRGuard&) = delete;
    EBRGuard& operator=(const EBRGuard&) = delete;
};

} // namespace lf
