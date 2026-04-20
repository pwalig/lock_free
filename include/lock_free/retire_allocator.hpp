#pragma once
#include <cstddef>
#include <memory>
#include <type_traits>
#include <set>
#include <lock_free/shared_thread_local.hpp>

namespace lock_free {
    template <typename T, size_t MaxThreads>
    struct retire_allocator {
        using value_type = T;
        using pointer = T*;
        using size_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::true_type;
        template< class U > struct rebind { typedef retire_allocator<U, MaxThreads> other; };
    private:
        struct thread_data {
            std::set<std::pair<pointer, size_type>> to_free;
#if defined(_MSC_VER) && !defined(__llvm__) && !defined(__INTEL_COMPILER)
            [[msvc::no_unique_address]] std::allocator<T> allocator;
#else
            [[no_unique_address]] std::allocator<T> allocator;
#endif
        };
        using shared_thread_local = shared_thread_local<thread_data, MaxThreads>;

    public:
        retire_allocator() = default;
        template <typename U>
        retire_allocator(const retire_allocator<U, MaxThreads>&) {};
        inline pointer allocate(size_type n, const void* hint = 0) {
            return std::allocator_traits<std::allocator<value_type>>::allocate(
                shared_thread_local::get().allocator,
                n, hint
            );
        }

        inline void deallocate(pointer p, size_type n = 1) {
            shared_thread_local::get().to_free.emplace(p, n);
        }

        inline static void cleanup() {
            for (auto& thd : shared_thread_local::thread_slots) {
                for (const std::pair<pointer, size_type>& p : thd->to_free) {
                    thd->allocator.deallocate(p.first, p.second);
                }
            }
        }
    };
}