// stack_ebr.hpp - Treiber stack with epoch-based reclamation

#pragma once

#include <atomic>
#include "common.hpp"
#include "epoch.hpp"

namespace lf {

template<typename T>
class StackEBR {
    struct Node {
        T value;
        Node* next;
        Node(const T& v) : value(v), next(nullptr) { on_alloc(); }
        ~Node() { on_free(); }
    };

    alignas(CACHE_LINE) std::atomic<Node*> head_{nullptr};

public:
    static constexpr const char* name() { return "EBR"; }

    StackEBR() = default;
    StackEBR(const StackEBR&) = delete;
    StackEBR& operator=(const StackEBR&) = delete;

    void push(const T& val) {
        EBRGuard g; // enter the epoch critical section
        Node* n = new Node(val);
        n->next = head_.load(std::memory_order_relaxed);
        while (!head_.compare_exchange_weak(n->next, n,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            // spin
        }
        // EBRGuard destruction exits the critical section.
    }

    bool pop(T& out) {
        EBRGuard g;
        Node* old_head = head_.load(std::memory_order_acquire);
        while (old_head) {
            // Inside the epoch critical section, no other thread can free
            // old_head: doing so requires the global epoch to advance twice
            // past the current one, and our local epoch is blocking that.
            Node* next = old_head->next;
            if (head_.compare_exchange_weak(old_head, next,
                    std::memory_order_acquire,
                    std::memory_order_acquire)) {
                out = old_head->value;
                ebr_mgr().retire(g.slot(), old_head, [](void* p) {
                    delete static_cast<Node*>(p);
                });
                return true;
            }
        }
        return false;
    }

    ~StackEBR() {
        Node* p = head_.load();
        while (p) {
            Node* next = p->next;
            delete p;
            p = next;
        }
    }
};

} // namespace lf
