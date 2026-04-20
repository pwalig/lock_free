#pragma once
#include <atomic>
#include <memory>
#include <optional>

// Implementation of the Michael & Scott Non-Blocking Queue.
// Requires C++20.
// You might need to compile with -mx32.
template <typename T>
struct michael_scott_non_blocking_queue {
    inline static constexpr size_t counted_pointer_alignment = sizeof(void*) + sizeof(size_t);
    inline static constexpr size_t default_node_alignment = alignof(T) > counted_pointer_alignment ? alignof(T) : counted_pointer_alignment;

    template <size_t NodeAlignment = default_node_alignment>
    struct detail {
        struct node;

        static_assert(
            sizeof(void*) == sizeof(node*),
            "node* has a different size, default alignment was not calculated correctly"
        );

        struct alignas(sizeof(node*) + sizeof(size_t)) counted_pointer {
            node* ptr = nullptr;
            size_t count = 0;

            inline counted_pointer(
                node* Ptr = nullptr,
                size_t Count = 0
            ) : ptr(Ptr), count(Count) {}

            inline node* get() { return ptr; }
            inline const node* get() const { return ptr; }
            inline node* operator->() { return ptr; }
            inline const node* operator->() const { return ptr; }
            inline node& operator*() { return *ptr; }
            inline const node& operator*() const { return *ptr; }

            friend inline bool operator==(counted_pointer lhs, counted_pointer rhs) {
                return (lhs.ptr == rhs.ptr) && (lhs.count == rhs.count);
            }
            friend inline bool operator==(node* lhs, counted_pointer rhs) {
                return lhs == rhs.ptr;
            }
            friend inline bool operator==(counted_pointer lhs, node* rhs) {
                return lhs.ptr == rhs;
            }
        };
        using atomic_counted_pointer = std::atomic<counted_pointer>;

        struct alignas(NodeAlignment) node {
            using value_type = T;

            value_type value;
            atomic_counted_pointer next;

            inline node() : value(), next() {}
            explicit inline node(const value_type& Value) : value(Value), next() {}
            explicit inline node(value_type&& Value) : value(std::move(Value)), next() {}
        };

        template <typename Alloc = std::allocator<T>>
        struct queue {
            using value_type = T;
            using reference = value_type&;
            using const_reference = const value_type&;

            using allocator_type = Alloc;
            using alloc_traits = std::allocator_traits<allocator_type>;
            using pointer = alloc_traits::pointer;
            using const_pointer = alloc_traits::const_pointer;
            using size_type = alloc_traits::size_type;
            using difference_type = alloc_traits::difference_type;

            using node_type = node;
            using node_allocator_type = alloc_traits::template rebind_alloc<node_type>;
            using node_alloc_traits = std::allocator_traits<node_allocator_type>;
            using node_pointer = node_alloc_traits::pointer;

        private:
            atomic_counted_pointer _head;
            atomic_counted_pointer _tail;
#if defined(_MSC_VER) && !defined(__llvm__) && !defined(__INTEL_COMPILER)
            [[msvc::no_unique_address]] node_allallocator_type _allocator;
#else
            [[no_unique_address]] node_allocator_type _allocator;
#endif

            inline void push_back(node_pointer node) {
                counted_pointer tail;
                while (true) {
                    tail = _tail.load();
                    counted_pointer next = tail->next.load();
                    if (tail == _tail.load()) { // if tail has not changed
                        if (next == nullptr) { // if tail points to the last node
                            // try to append node at the end of the queue
                            if (tail->next.compare_exchange_strong(next, { node, next.count + 1 })) break;
                        }
                        else {
                            // tail was not pointing to the last node, try to update it
                            _tail.compare_exchange_strong(tail, { next.ptr, tail.count + 1 });
                        }
                    }
                }
                // try to move tail to inserted node
                _tail.compare_exchange_strong(tail, { node, tail.count + 1 });
            }

        public:
            inline queue(
                const allocator_type& Aloc = allocator_type()
            ) : _head(), _tail(), _allocator(node_allocator_type(Aloc)) {
                node_pointer node = node_alloc_traits::allocate(_allocator, 1);
                node_alloc_traits::construct(_allocator, node);
                _head.store(node, std::memory_order::relaxed);
                _tail.store(node, std::memory_order::relaxed);
            }

            inline void push_back(const_reference Value) {
                node_pointer node = node_alloc_traits::allocate(_allocator, 1);
                node_alloc_traits::construct(_allocator, node, Value);
                push_back(node);
            }
            inline void push_back(value_type&& Value) {
                node_pointer node = node_alloc_traits::allocate(_allocator, 1);
                node_alloc_traits::construct(_allocator, node, std::move(Value));
                push_back(node);
            }
            inline void emplace_back(auto&&... args) {
                node_pointer node = node_alloc_traits::allocate(_allocator, 1);
                node_alloc_traits::construct(
                    _allocator, node,
                    value_type(std::forward<decltype(args)>(args)...)
                );
                push_back(node);
            }

            inline std::optional<value_type> pop_front() {
                value_type value;
                counted_pointer head;
                while (true) {
                    head = _head.load();
                    counted_pointer tail = _tail.load();
                    counted_pointer next = head->next.load();
                    if (head == _head.load()) {
                        if (head.ptr == tail.ptr) {
                            if (next == nullptr) return std::nullopt; // queue empty
                            else _tail.compare_exchange_strong(tail, { next.ptr, tail.count + 1 }); // tail falling behind
                        }
                        else {
                            value = next.ptr->value; // read value

                            // try update head to next node
                            if (_head.compare_exchange_strong(head, { next.ptr, head.count + 1})) break;
                        }
                    }
                }
                node_alloc_traits::destroy(_allocator, head.ptr);
                node_alloc_traits::deallocate(_allocator, head.ptr, 1);
                return value;
            }

            // Implemented for completness sake.
            // Does not guarantee that returned value is
            // at the back in multithreaded envirionment.
            inline std::optional<value_type> back() const {
                counted_pointer cptr = _tail.load()->next.load();
                if (cptr == nullptr) return std::nullopt;
                else return *cptr;
            }

            // Implemented for completness sake.
            // Does not guarantee that returned value is
            // at the front in multithreaded envirionment.
            inline std::optional<value_type> front() const {
                counted_pointer cptr = _head.load()->next.load();
                if (cptr == nullptr) return std::nullopt;
                else return *cptr;
            }

            // if this returns false try to compile with -mx32 option
            inline bool is_lock_free() { return atomic_counted_pointer{}.is_lock_free(); }

            // if this is false try to compile with -mx32 option
            inline static constexpr bool is_always_lock_free = atomic_counted_pointer::is_always_lock_free;

            constexpr inline allocator_type get_allocator() const {
                return allocator_type(_allocator);
            }
        };

        static_assert(
            sizeof(queue<>) == 2 * sizeof(atomic_counted_pointer),
            "Compiler did not properly optimize size of queue."
            "Feel free to delete this assertion if you are OK with slightly bigger queue."
        );

        // static_assert(queue<>::is_always_lock_free, "queue is not lock free, try to compile with -mx32");
    };

    template <size_t NodeAlignment = default_node_alignment>
    using node = detail<NodeAlignment>::node;

    template <size_t NodeAlignment = default_node_alignment>
    using counted_pointer = detail<NodeAlignment>::counted_pointer;

    template <size_t NodeAlignment = default_node_alignment>
    using atomic_counted_pointer = detail<NodeAlignment>::atomic_counted_pointer;

    template <size_t NodeAlignment = default_node_alignment,
        typename Allocator = std::allocator<node<NodeAlignment>>>
    using queue = detail<NodeAlignment>::template queue<Allocator>;
};

// Implementation of the Michael & Scott Non-Blocking Queue.
// Requires C++20.
// You might need to compile with and -mx32.
template<typename T,
    size_t NodeAlignment = michael_scott_non_blocking_queue<T>::default_node_alignment,
    typename Allocator = std::allocator<typename michael_scott_non_blocking_queue<T>::template node<NodeAlignment>>>
using msnb_queue = michael_scott_non_blocking_queue<T>::template queue<NodeAlignment, Allocator>;