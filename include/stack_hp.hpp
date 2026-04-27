// stack_hp.hpp - Treiber stack with hazard-pointer reclamation

#pragma once

#include <atomic>
#include "common.hpp"
#include "hazard_pointers.hpp"

namespace lf {

template<typename T>
class StackHP {
    struct Node {
        T value;
        Node* next;
        Node(const T& v) : value(v), next(nullptr) { on_alloc(); }
        ~Node() { on_free(); }
    };

    alignas(CACHE_LINE) std::atomic<Node*> head_{nullptr};

public:
    static constexpr const char* name() { return "HP"; }

    StackHP() = default;
    StackHP(const StackHP&) = delete;
    StackHP& operator=(const StackHP&) = delete;

    void push(const T& val) {
        Node* n = new Node(val);
        n->next = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(n->next, n,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            // spin
        }
    }

    bool pop(T& out) {
        int slot = this_thread_hp().slot();
        auto& mgr = hp_mgr();

        Node* old_head;
        while (true) {
            // Classic HP read protocol: load -> publish HP -> reload to verify.
            old_head = head_.load(std::memory_order_acquire);
            if (!old_head) return false;

            mgr.protect(slot, 0, old_head); // mark old_head as hazardous

            // Critical: re-read head_ to confirm it has not changed.
            // If a concurrent pop retired old_head before our publication
            // became visible, head_ now points elsewhere and we must retry.
            if (head_.load(std::memory_order_acquire) != old_head) continue;

            // old_head is now protected by our hazard pointer, so reading
            // its next is safe.
            Node* next = old_head->next;
            if (head_.compare_exchange_weak(old_head, next,
                    std::memory_order_acquire,
                    std::memory_order_acquire)) {
                out = old_head->value;
                mgr.clear(slot, 0); // protection no longer needed
                mgr.retire(slot, old_head, [](void* p) {
                    delete static_cast<Node*>(p);
                });
                return true;
            }
            // CAS failed; old_head was refreshed, retry.
        }
    }

    ~StackHP() {
        // Single-threaded teardown.
        Node* p = head_.load();
        while (p) {
            Node* next = p->next;
            delete p;
            p = next;
        }
    }
};

} // namespace lf
