#include <cstddef>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <array>
#include <list>
#include <cassert>

namespace lock_free {
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
    class hazard_domain {
        struct alignas(HardwareDestructiveInterferenceSize) thread_data {
            std::atomic<std::thread::id> thread_id;
            std::array<std::atomic<T*>, SlotsPerThread> slots;
            std::list<T*> to_free;
            thread_data() : thread_id(std::thread::id()), slots(), to_free() { }
        };
        inline static std::array<thread_data, MaxThreads> thread_slots;

        class thread_data_owner {
            thread_data* _data;

        public:
            thread_data_owner() : _data(nullptr) {
                for (auto& slot : thread_slots) {
                    std::thread::id old_id = std::thread::id();
                    if (slot.thread_id.compare_exchange_strong(old_id, std::this_thread::get_id())) {
                        _data = &slot;
                        break;
                    }
                }
                if (_data == nullptr) throw std::runtime_error("No hazard pointers available!");
            }

            inline thread_data* data() { return _data; }
            inline const thread_data* data() const { return _data; }
            inline thread_data& value() { return *_data; }
            inline const thread_data& value() const { return *_data; }

            ~thread_data_owner() {
                _data->thread_id.store(std::thread::id());
            }
        };

        inline static thread_data& get_thread_data() {
            thread_local thread_data_owner owner;
            return owner.value();
        }

        inline static void do_free(const auto& deleter, std::list<T*>& to_free) {
            auto it = to_free.begin();
            while (it != to_free.end()) {
                bool used = false;
                for (thread_data& thd : thread_slots) {
                    if (thd.thread_id != std::thread::id()) {
                        for (auto& slot : thd.slots) {
                            if (slot.load() == *it)  {
                                used = true;
                                break;
                            }
                        }
                        if (used) break;
                    }
                }
                if (!used) {
                    deleter(*it);
                    it = to_free.erase(it);
                }
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
            thread_data& data = get_thread_data();
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

        // Marks pointer previously marked as used by `use` as unused,
        // which allows it to be freed.
        inline static void release(size_t slot = 0) {
            assert(slot < SlotsPerThread);
            thread_data& data = get_thread_data();
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
            thread_data& data = get_thread_data();
            data.to_free.push_back(ptr);
        }

        // Frees unused (see `use`) pointers marked on this_thread by
        // `retire(T*)` as "to be freed".
        inline static void free(const auto& deleter) {
            thread_data& data = get_thread_data();
            do_free(deleter, data.to_free);
        }

        // Same as `free(const auto&)`, but frees pointers marked by all threads.
        // Assumes single threaded access
        // (no other thread is performing `free` or `retire` operations).
        inline static void free_all(const auto& deleter) {
            for (auto& slot : thread_slots) {
                // assert(slot.thread_id.load(std::memory_order::relaxed) != std::thread::id());
                do_free(deleter, slot.to_free);
            }
        }
        
        // Smart pointer that prevents assigned pointer from being freed by other threads.
        // Uses hazard pointer mechanism (https://en.wikipedia.org/wiki/Hazard_pointer).
        // Wraps `hazard_domain` 's `use`, `release` and `retire` static methods.
        template <size_t Slot = 0>
        struct ptr {
            using element_type = T;
            using pointer = T*;
        private:
            using domain = hazard_domain;
            T* _ptr;
        public:
            constexpr ptr() noexcept : _ptr(nullptr) {}

            explicit ptr(const std::atomic<T*>& Ptr) :
                _ptr(domain::use<Slot>(Ptr)) {}

            ptr(const ptr&) = delete;

            ptr(ptr&& other) noexcept : _ptr(other._ptr) {
                other._ptr = nullptr;
            };

            ptr& operator=(const ptr&) = delete;
            ptr& operator=(ptr&& other) noexcept {
                if (this != &other) {
                    _ptr = other._ptr;
                    other._ptr = nullptr;
                }
            }

            ptr& operator=(const std::atomic<T*>& Ptr) {
                _ptr = domain::use<Slot>(Ptr);
            }

            ~ptr() {
                if (_ptr != nullptr) {
                    domain::release<Slot>();
                    _ptr = nullptr;
                }
            }

            T* operator->() { return _ptr; }
            const T* operator->() const { return _ptr; }
            T& operator*() { return *_ptr; }
            const T& operator*() const { return *_ptr; }
            T* get() { return _ptr; }
            const T* get() const { return _ptr; }
            explicit operator bool() const noexcept {
                return _ptr != nullptr;
            }

            template <size_t Slot2>
            inline friend bool operator==(const ptr& lhs, const ptr<Slot2>& rhs) {
                return lhs.get() == rhs.get();
            }

            // releases held pointer
            void release() {
                domain::release<Slot>();
                _ptr = nullptr;
            }
            
            // replaces held pointer
            void reset(const std::atomic<T*>& Ptr) {
                _ptr = domain::use<Slot>(Ptr);
            }

            // releases and retires held pointer
            void retire() {
                domain::release<Slot>();
                domain::retire(_ptr);
                _ptr = nullptr;
            }
        };
        using ptr0 = ptr<0>;
        using ptr1 = ptr<1>;
        using ptr2 = ptr<2>;
        using ptr3 = ptr<3>;
        using ptr4 = ptr<4>;
    };

    template <typename T>
    inline constexpr bool is_hazard_domain = false;

    template <typename T, size_t MaxThreads, size_t SlotsPerThread>
    inline constexpr bool is_hazard_domain<hazard_domain<T, MaxThreads, SlotsPerThread>> = true;

    template <typename T>
    concept HazardDomain = is_hazard_domain<T>;

    // Smart pointer that prevents assigned pointer from being freed by other threads.
    // Uses hazard pointer mechanism (https://en.wikipedia.org/wiki/Hazard_pointer).
    // Wraps `hazard_domain` 's `use`, `release` and `retire` static methods.
    template <HazardDomain Domain, size_t Slot = 0>
    using hazard_ptr = Domain::template ptr<Slot>;
}