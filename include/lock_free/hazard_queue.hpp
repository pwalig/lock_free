#include <atomic>
#include <memory>
#include <optional>
#include <lock_free/hazptr/domain.hpp>

namespace lock_free {
    // Implementation of Michael and Scott non-blocking queue
    // (https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf),
    // but using hazard pointers to solve the ABA problem.
    template <typename T, size_t MaxThreads, size_t NodeAlignment = alignof(T), typename Alloc = std::allocator<T>>
    struct hazard_queue {
        using value_type = T;
        using reference = value_type&;
        using const_reference = const value_type&;

        using allocator_type = Alloc;
        using alloc_traits = std::allocator_traits<allocator_type>;
        using pointer = alloc_traits::pointer;
        using const_pointer = alloc_traits::const_pointer;
        using size_type = alloc_traits::size_type;
        using difference_type = alloc_traits::difference_type;

        inline static constexpr size_t node_alignment = NodeAlignment > alignof(std::atomic<void*>) ? NodeAlignment : alignof(std::atomic<void*>);

        struct alignas(node_alignment) node_type {
            using value_type = T;

            value_type value;
            std::atomic<node_type*> next;

            static_assert(std::atomic<node_type*>::is_always_lock_free);
        };
        using node_allocator_type = alloc_traits::template rebind_alloc<node_type>;
        using node_alloc_traits = std::allocator_traits<node_allocator_type>;
        using node_pointer = node_alloc_traits::pointer;
        using atomic_node_pointer = std::atomic<node_pointer>;
        using hazard_domain = hazptr::domain<node_type, MaxThreads, 2>;

    private:
        using hazard = hazard_domain;
        using hazard_ptr0 = hazard::ptr0;
        using hazard_ptr1 = hazard::ptr1;

        atomic_node_pointer _head;
        atomic_node_pointer _tail;
#if defined(_MSC_VER) && !defined(__llvm__) && !defined(__INTEL_COMPILER)
        [[msvc::no_unique_address]] node_allocator_type _allocator;
#else
        [[no_unique_address]] node_allocator_type _allocator;
#endif

        inline void push(node_pointer node) {
            node_pointer tail;
            while (true) {
                tail = hazard::acquire0(_tail);
                node_pointer next = tail->next.load();
                if (tail == _tail.load()) { // if tail has not changed
                    if (next == nullptr) { // if tail points to the last node
                        // try to append node at the end of the queue
                        if (tail->next.compare_exchange_strong(next, node)) break;
                    }
                    else {
                        // tail was not pointing to the last node, try to update it
                        _tail.compare_exchange_strong(tail, next);
                    }
                }
            }
            // try to move tail to inserted node
            _tail.compare_exchange_strong(tail, node);
            hazard::release0();
        }

    public:
        inline hazard_queue(
            const allocator_type& Aloc = allocator_type()
        ) : _head(), _tail(), _allocator(node_allocator_type(Aloc)) {
            node_pointer node = node_alloc_traits::allocate(_allocator, 1);
            node_alloc_traits::construct(_allocator, node);
            _head.store(node, std::memory_order::relaxed);
            _tail.store(node, std::memory_order::relaxed);
        }

        inline void push(const_reference Value) {
            node_pointer node = node_alloc_traits::allocate(_allocator, 1);
            node_alloc_traits::construct(_allocator, node, Value);
            push(node);
        }
        inline void push(value_type&& Value) {
            node_pointer node = node_alloc_traits::allocate(_allocator, 1);
            node_alloc_traits::construct(_allocator, node, std::move(Value));
            push(node);
        }
        inline void push_back(const_reference Value) {
            push(Value);
        }
        inline void push_back(value_type&& Value) {
            push(std::move(Value));
        }

        inline std::optional<value_type> pop() {
            value_type value;
            node_pointer head;
            while (true) {
                head = hazard::acquire0(_head);
                node_pointer tail = _tail.load();
                node_pointer next = hazard::acquire1(head->next);
                if (head == _head.load()) { // if head, tail and next are consistent
                    if (head == tail) { // if queue empty or tail falling behind
                        if (next == nullptr) {
                            hazard::release0(); // 'next' releases in destructor
                            hazard::release1();
                            return std::nullopt; // queue empty
                        }
                        else _tail.compare_exchange_strong(tail, next); // tail falling behind
                    }
                    else {
                        value = next->value; // read value
                        hazard::release1();

                        // try update head to next node
                        if (_head.compare_exchange_strong(head, next)) break;
                    }
                }
            }
            node_alloc_traits::destroy(_allocator, head);
            hazard::release0();
            hazard::retire(head);
            return value;
        }

        inline std::optional<value_type> pop_front() {
            return pop();
        }

        // Frees unused nodes.
        // Call this from time to time to reclaim memory.
        // Calling this function is optional as all memory
        // will be reclaimed in `hazard_queue` destructor.
        inline void free_garbage_memory() {
            hazard::free([this](node_pointer ptr) {
                node_alloc_traits::deallocate(_allocator, ptr, 1);
            });
        }

        // Assumes that more than no more than one thread has access to this queue.
        inline ~hazard_queue() {
            hazard::free_all([this](node_pointer ptr){
                node_alloc_traits::deallocate(_allocator, ptr, 1);
            });
            node_pointer node = _head.load(std::memory_order::relaxed);
            while (node) {
                node_pointer tmp = node;
                node_alloc_traits::destroy(_allocator, tmp);
                node_alloc_traits::deallocate(_allocator, tmp, 1);
                node = node->next.load(std::memory_order::relaxed);
            }
        }

        inline bool is_lock_free() { return atomic_node_pointer{}.is_lock_free(); }

        inline static constexpr bool is_always_lock_free = atomic_node_pointer::is_always_lock_free;
        static_assert(is_always_lock_free, "hazard_queue not lock free");

        constexpr inline allocator_type get_allocator() const {
            return allocator_type(_allocator);
        }
    };
}