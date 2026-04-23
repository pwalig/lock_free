#pragma once
#include <cstddef>
#include <thread>
#include <atomic>
#include <cassert>
#include <lock_free/hazptr.hpp>
#include <lock_free/shared_thread_local.hpp>
#include <lock_free/hazptr/domain_traits.hpp>
#include <unordered_set>
#include <utility>

namespace lock_free::hazptr {
    // Hazard pointer (https://en.wikipedia.org/wiki/Hazard_pointer) implementation.
    // The most dynamic implementation. It supports any number of hazard pointers of any type per thread.
    template <typename Deleter, size_t MaxThreads = 0>
    struct static_void_domain {
        using pointer = void*;
        using atomic_pointer = std::atomic<pointer>;

    private:
        using set_of_pointers = std::unordered_set<pointer>;
        struct thread_data {
            std::atomic<set_of_pointers*> hazard_hazard_pointer;
            std::atomic<set_of_pointers*> hazard_pointers;
            std::unordered_set<set_of_pointers*> hazard_retire_list;
            std::unordered_set<std::pair<pointer, Deleter>> retire_list;
            thread_data() :
                hazard_hazard_pointer(nullptr),
                hazard_pointers(new set_of_pointers()),
                hazard_retire_list(), retire_list() { }
        };
        using _shared_thread_local = shared_thread_local<thread_data, MaxThreads>;

    public:
        inline static constexpr bool is_always_lock_free = std::atomic<set_of_pointers*>::is_always_lock_free;
        static_assert(is_always_lock_free, "hazard pointer is not lock free on this platform");
        bool is_lock_free() const { return std::atomic<set_of_pointers*>{}.is_lock_free(); }

        // Atomically loads supplied atomic pointer and marks it as used,
        // which prevents it from being freed until `release` on same pointer is called.
        template <typename T>
        inline static T* protect(const std::atomic<T*>& Ptr) {
            thread_data& data = _shared_thread_local();
            T* ptr;
            set_of_pointers* old_hazard_pointers = data.hazard_pointers.load();
            data.hazard_retire_list.insert(old_hazard_pointers);
            while (true) {
                set_of_pointers* new_hazard_pointers = new set_of_pointers(*old_hazard_pointers);
                ptr = Ptr.load();
                new_hazard_pointers->insert(ptr);
                data.hazard_pointers.store(new_hazard_pointers);
                if (ptr == Ptr.load()) break;
                else data.hazard_retire_list.insert(new_hazard_pointers);
            }
            return ptr;
        }

        // Atomically loads supplied atomic pointer and marks it as used,
        // which prevents it from being freed until `release` on same pointer is called.
        template <typename T>
        inline static bool try_protect(T*& ptr, const std::atomic<T*>& Ptr) {
            thread_data& data = _shared_thread_local();
            set_of_pointers* old_hazard_pointers = data.hazard_pointers.load();
            set_of_pointers* new_hazard_pointers = new set_of_pointers(*old_hazard_pointers);
            ptr = Ptr.load();
            new_hazard_pointers->insert(ptr);
            data.hazard_pointers.store(new_hazard_pointers);
            if (ptr == Ptr.load()) {
                data.hazard_retire_list.insert(old_hazard_pointers);
                return true;
            }
            else {
                data.hazard_pointers.store(old_hazard_pointers);
                data.hazard_retire_list.insert(new_hazard_pointers);
                return false;
            }
        }

        template <typename T>
        inline static void release(T* Ptr) {
            thread_data& data = _shared_thread_local();
            set_of_pointers* old_hazard_pointers = data.hazard_pointers.load();
            set_of_pointers* new_hazard_pointers = new set_of_pointers(*old_hazard_pointers);
            new_hazard_pointers->erase(Ptr);
            data.hazard_pointers.store(new_hazard_pointers);
            data.hazard_retire_list.insert(old_hazard_pointers);
        }

        template <typename T>
        inline static void retire(T* Ptr, Deleter deleter) {
            thread_data& data = _shared_thread_local();
            data.retire_list.emplace(Ptr, std::move(deleter));
        }

        // Frees unused data marked to be freed.
        // Returns weather all data has been freed,
        // so that you can and thread with: `while(!free()) {}`.
        inline static bool free() {
            thread_data& data = _shared_thread_local();

            // free from retire_list
            {
                auto it = data.retire_list.begin();
                while(it != data.retire_list.end()) {
                    bool used = false;
                    for (auto& thd : _shared_thread_local::thread_slots) {
                        if (thd.thread_id == std::thread::id()) continue;
                        set_of_pointers* hazard_pointers = hazptr::protect(
                            data.hazard_hazard_pointer,
                            thd->hazard_pointers
                        );
                        if (hazard_pointers->find(it->first) != hazard_pointers->end()) used = true;
                        data.hazard_hazard_pointer.store(nullptr);
                        if (used) break;
                    }
                    if (!used) {
                        it->second(it->first); // call deleter
                        it = data.retire_list.erase(it);
                    } else ++it;
                }
            }
            if (data.retire_list.empty())
            
            // free from hazard_retire_list
            {
                auto it = data.hazard_retire_list.begin();
                while (it != data.hazard_retire_list.end()) {
                    bool used = false;
                    for (auto& thd : _shared_thread_local::thread_slots) {
                        if (thd.thread_id == std::thread::id()) continue;
                        if (thd->hazard_hazard_pointer.load() == *it) {
                            used = true;
                            break;
                        }
                    }
                    if (!used) {
                        delete *it;
                        it = data.hazard_retire_list.erase(it);
                    } else ++it;
                }
            }

            return ((!data.retire_list.empty()) && (!data.hazard_retire_list.empty()));
        }

        struct pointer_guard {
            using domain = static_void_domain;

        private:
            pointer _ptr = nullptr;

        public:
            template <typename T>
            T* protect(const std::atomic<T*>& Ptr) const {
                if (_ptr != nullptr) domain::release(_ptr);
                return _ptr = domain::protect(Ptr);
            }

            ~pointer_guard() {
                if (_ptr != nullptr) domain::release(_ptr);
            }
        };

        template <typename T>
        struct guarded_ptr {
            using domain = static_void_domain;
            using pointer = T*;
            using const_pointer = const T*;
            using reference = T&;
            using const_reference = const T&;

        private:
            pointer _ptr;

        public:
            guarded_ptr(const std::atomic<T*>& Ptr) : _ptr(domain::protect(Ptr)) {}
            guarded_ptr(const guarded_ptr&) = delete;
            guarded_ptr(guarded_ptr&&) = delete;
            guarded_ptr& operator=(const guarded_ptr&) = delete;
            guarded_ptr& operator=(guarded_ptr&&) = delete;
            ~guarded_ptr() { domain::release(_ptr); }

            void reset(const std::atomic<T*>& Ptr) {
                domain::release(_ptr);
                _ptr = domain::protect(Ptr);
            }

            pointer get() { return _ptr; }
            const_pointer get() const { return _ptr; }
            reference operator*() { return *_ptr; }
            const_reference operator*() const { return *_ptr; }
            pointer operator->() { return _ptr; }
            const_pointer operator->() const { return _ptr; }

            explicit operator bool() const { return _ptr != nullptr; }
            inline friend bool operator==(const guarded_ptr& lhs, std::nullptr_t rhs) { return lhs._ptr == rhs; }
            inline friend bool operator!=(const guarded_ptr& lhs, std::nullptr_t rhs) { return lhs._ptr != rhs; }
            inline friend bool operator==(std::nullptr_t lhs, const guarded_ptr& rhs) { return lhs == rhs._ptr; }
            inline friend bool operator!=(std::nullptr_t lhs, const guarded_ptr& rhs) { return lhs != rhs._ptr; }

            inline friend bool operator==(const guarded_ptr& lhs, const guarded_ptr& rhs) { return lhs._ptr == rhs._ptr; }
            inline friend bool operator!=(const guarded_ptr& lhs, const guarded_ptr& rhs) { return lhs._ptr != rhs._ptr; }
            inline friend bool operator<(const guarded_ptr& lhs, const guarded_ptr& rhs) { return lhs._ptr < rhs._ptr; }
            inline friend bool operator>(const guarded_ptr& lhs, const guarded_ptr& rhs) { return lhs._ptr > rhs._ptr; }
            inline friend bool operator<(const guarded_ptr& lhs, const guarded_ptr& rhs) { return lhs._ptr <= rhs._ptr; }
            inline friend bool operator>=(const guarded_ptr& lhs, const guarded_ptr& rhs) { return lhs._ptr >= rhs._ptr; }
        };
    };

    template <typename Deleter, size_t MaxThreads>
    inline constexpr bool is_domain<static_void_domain<Deleter, MaxThreads>> = true;
}