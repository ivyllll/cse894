// stack_rc.hpp - Reference counting via std::shared_ptr atomic operations
//
// Maintains a Treiber stack using std::shared_ptr atomic operations to
// guarantee memory safety. The interface looks lock-free, but on libstdc++
// the underlying std::atomic_load(shared_ptr*) and
// std::atomic_compare_exchange_weak(shared_ptr*, ...) operations fall back
// to a global spinlock pool keyed on the shared_ptr address. Two threads
// that contend on the same head pointer therefore serialize on the same
// lock. This is one of the project's key findings.
//
// We use the C++17 free-standing atomic operations on shared_ptr. They are
// deprecated in C++20 (replaced by std::atomic<std::shared_ptr<T>>) but
// remain widely supported.

#pragma once

#include <atomic>
#include <memory>
#include "common.hpp"

namespace lf {

template<typename T>
class StackRC {
    struct Node {
        T value;
        std::shared_ptr<Node> next;
        Node(const T& v) : value(v) { on_alloc(); }
        ~Node() { on_free(); }
    };

    alignas(CACHE_LINE) std::shared_ptr<Node> head_;

public:
    static constexpr const char* name() { return "RC"; }

    StackRC() = default;
    StackRC(const StackRC&) = delete;
    StackRC& operator=(const StackRC&) = delete;

    void push(const T& val) {
        auto new_node = std::make_shared<Node>(val);
        new_node->next = std::atomic_load(&head_);
        // On CAS failure, new_node->next is atomically refreshed.
        while (!std::atomic_compare_exchange_weak(
                &head_, &new_node->next, new_node)) {
            // spin
        }
    }

    bool pop(T& out) {
        auto old_head = std::atomic_load(&head_);
        while (old_head) {
            // While we hold a local shared_ptr to the node, its refcount is
            // at least one, so the node cannot be freed. Reading old_head->next
            // is therefore safe -- this is the central RC invariant.
            if (std::atomic_compare_exchange_weak(
                    &head_, &old_head, old_head->next)) {
                out = old_head->value;
                on_retire();
                // When this local shared_ptr goes out of scope its refcount
                // decrements; if it was the last one the Node is destroyed
                // and on_free() runs.
                return true;
            }
        }
        return false;
    }

    ~StackRC() {
        // Avoid stack-overflow from deeply recursive shared_ptr chain
        // destruction by unrolling manually.
        auto p = std::atomic_load(&head_);
        std::atomic_store(&head_, std::shared_ptr<Node>());
        while (p) {
            auto next = std::move(p->next);
            p.reset();
            p = std::move(next);
        }
    }
};

} // namespace lf
