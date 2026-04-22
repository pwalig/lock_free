#pragma once
#include <cstddef>
#include <memory>
#include <thread>
#include <atomic>
#include <array>
#include <list>
#include <cassert>
#include <lock_free/shared_thread_local.hpp>
#include <lock_free/hazptr/domain_traits.hpp>

namespace lock_free::hazptr {
    // Hazard pointer (https://en.wikipedia.org/wiki/Hazard_pointer) implementation.
    // Based on: https://www.modernescpp.com/index.php/a-lock-free-stack-a-hazard-pointer-implementation/,
    // but with actually lock free memory releasing algorithm
    // and extended to many hazard pointers per thread.
    template<
        typename T,
        size_t MaxThreads = 2,
        size_t SlotsPerThread = 1,
        size_t HardwareDestructiveInterferenceSize = 64
    >
    struct static_slot_domain {
        using value_type = T;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using atomic_pointer = std::atomic<pointer>;

    private:
        size_t _slot;

        struct alignas(HardwareDestructiveInterferenceSize) thread_data {
            std::array<std::atomic<T*>, SlotsPerThread> slots;
            std::list<T*> to_free;
            thread_data() : slots(), to_free() { }
        };
        using _shared_thread_local = shared_thread_local<thread_data, MaxThreads>;

        inline static void do_free(
            std::list<T*>& to_free,
            const auto& deleter = std::default_delete<T>()
        ) {
            auto it = to_free.begin();
            while (it != to_free.end()) {
                bool used = false;
                for (auto& thd : _shared_thread_local::thread_slots) {
                    if (thd.thread_id.load() != std::thread::id()) continue;
                    for (auto& slot : thd->slots) {
                        if (slot.load() == *it) used = true;
                        if (used) break;
                    }
                    if (used) break;
                }
                if (!used) {
                    deleter(*it);
                    it = to_free.erase(it);
                } else ++it;
            }
        }

    public:
        inline static constexpr bool is_always_lock_free =
            std::atomic<std::thread::id>::is_always_lock_free &&
            std::atomic<T*>::is_always_lock_free;

        static_assert(is_always_lock_free, "hazard pointer is not lock free on this platform");

        // Atomically loads supplied atomic pointer and marks it as used,
        // which prevents it from being freed until `release` on same slot is called.
        inline static T* use(const std::atomic<T*>& aptr, size_t slot) {
            assert(slot < SlotsPerThread);
            thread_data& data = _shared_thread_local();
            T* ptr;
            do {
                ptr = aptr.load();
                data.slots[slot].store(ptr);
            } while (ptr != aptr.load());
            return ptr;
        }

        // Same as `use(const std::atomic<T*>&, size_t)`,
        // but with compile time range checking.
        template <size_t Slot = 0>
        inline static T* use(const std::atomic<T*>& aptr) {
            static_assert(Slot < SlotsPerThread, "Slot index out of range!");
            return use(aptr, Slot);
        }
        inline static T* use0(const std::atomic<T*>& aptr) { return use<0>(aptr); }
        inline static T* use1(const std::atomic<T*>& aptr) { return use<1>(aptr); }
        inline static T* use2(const std::atomic<T*>& aptr) { return use<2>(aptr); }
        inline static T* use3(const std::atomic<T*>& aptr) { return use<3>(aptr); }
        inline static T* use4(const std::atomic<T*>& aptr) { return use<4>(aptr); }

        // same as `use`
        template <size_t Slot = 0>
        inline static T* acquire(const std::atomic<T*>& aptr) {
            return use<Slot>(aptr);
        }
        // same as `use`
        inline static T* acquire(const std::atomic<T*>& aptr, size_t slot = 0) {
            return use(aptr, slot);
        }
        inline static T* acquire0(const std::atomic<T*>& aptr) { return acquire<0>(aptr); }
        inline static T* acquire1(const std::atomic<T*>& aptr) { return acquire<1>(aptr); }
        inline static T* acquire2(const std::atomic<T*>& aptr) { return acquire<2>(aptr); }
        inline static T* acquire3(const std::atomic<T*>& aptr) { return acquire<3>(aptr); }
        inline static T* acquire4(const std::atomic<T*>& aptr) { return acquire<4>(aptr); }

        // Marks pointer previously marked as used by `use` as unused,
        // which allows it to be freed.
        inline static void release(size_t slot = 0) {
            assert(slot < SlotsPerThread);
            thread_data& data = _shared_thread_local();
            data.slots[slot].store(nullptr);
        }

        // Same as `release(size_t slot)`,
        // but with compile time range checking.
        template <size_t Slot = 0>
        inline static void release() {
            static_assert(Slot < SlotsPerThread, "Slot index out of range!");
            release(Slot);
        }

        inline static void release0() { return release<0>(); }
        inline static void release1() { return release<1>(); }
        inline static void release2() { return release<2>(); }
        inline static void release3() { return release<3>(); }
        inline static void release4() { return release<4>(); }

        // Marks pointer as "to be freed".
        inline static void retire(T* ptr) {
            thread_data& data = _shared_thread_local();
            data.to_free.push_back(ptr);
        }

        // Frees unused (see `use`) pointers marked on this_thread by
        // `retire(T*)` as "to be freed".
        inline static void free(const auto& deleter = std::default_delete<T>()) {
            thread_data& data = _shared_thread_local();
            do_free(data.to_free, deleter);
        }

        // Same as `free(const auto&)`, but frees pointers marked by all threads.
        // Assumes single threaded access
        // (no other thread is performing `free` or `retire` operations).
        inline static void free_all(const auto& deleter = std::default_delete<T>()) {
            for (auto& slot : _shared_thread_local::thread_slots)
                do_free(slot->to_free, deleter);
        }

        pointer protect(const atomic_pointer& Ptr) {
            return use(Ptr, _slot);
        }
        void release(pointer) {
            release(_slot);
        }
        void retire(pointer Ptr, [[maybe_unused]]const auto& deleter = std::default_delete<T>()) {
            retire(Ptr);
        }
    };

    template <typename T, size_t MaxThreads, size_t SlotsPerThread>
    inline constexpr bool is_domain<static_slot_domain<T, MaxThreads, SlotsPerThread>> = true;
}