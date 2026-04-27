// stack_leak.hpp - Treiber stack with intentional memory leak
//
// Performance-ceiling baseline. We never reclaim popped nodes, so reading
// old->next is unconditionally safe (no node is ever freed while the
// program runs). It models "what is the throughput if we ignore the memory
// safety problem entirely?".

#pragma once

#include <atomic>
#include "common.hpp"

namespace lf {

template<typename T>
class StackLeak {
    struct Node {
        T value;
        Node* next;
        Node(const T& v) : value(v), next(nullptr) { on_alloc(); }
        ~Node() { on_free(); }
    };

    alignas(CACHE_LINE) std::atomic<Node*> head_{nullptr};

public:
    static constexpr const char* name() { return "Leak"; }

    StackLeak() = default;
    StackLeak(const StackLeak&) = delete;
    StackLeak& operator=(const StackLeak&) = delete;

    void push(const T& val) {
        Node* n = new Node(val);
        n->next = head_.load(std::memory_order_relaxed);
        // On CAS failure, n->next is updated to the latest head_ for retry.
        while (!head_.compare_exchange_weak(n->next, n,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            // spin
        }
    }

    bool pop(T& out) {
        Node* old_head = head_.load(std::memory_order_acquire);
        while (old_head) {
            // In a real implementation this load is the use-after-free risk
            // point. Here we never free nodes, so it is always safe.
            // The whole purpose of the RC/HP/EBR variants is to make this
            // load safe under reclamation.
            Node* next = old_head->next;
            if (head_.compare_exchange_weak(old_head, next,
                    std::memory_order_acquire,
                    std::memory_order_acquire)) {
                out = old_head->value;
                on_retire();
                // Intentionally do NOT delete old_head -- this is the leak baseline.
                return true;
            }
            // CAS failure refreshes old_head with the current head_.
        }
        return false;
    }

    ~StackLeak() {
        // Single-threaded teardown to keep tools like valgrind happy.
        Node* p = head_.load();
        while (p) {
            Node* next = p->next;
            delete p;
            p = next;
        }
    }
};

} // namespace lf
