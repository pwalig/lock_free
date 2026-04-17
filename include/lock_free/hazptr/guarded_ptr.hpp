#include <cstddef>
#include <atomic>
#include <cassert>
#include <lock_free/hazptr/is_domain.hpp>

namespace lock_free::hazptr {
    // Smart pointer that prevents assigned pointer from being freed by other threads.
    // Uses hazard pointer mechanism (https://en.wikipedia.org/wiki/Hazard_pointer).
    // Wraps `Domain` 's `use`, `release` and `retire` static methods.
    template <Domain HazardDomain, size_t Slot = 0>
    struct guarded_ptr {
        using domain = HazardDomain;
        using element_type = domain::value_type;
        using pointer = element_type*;
        using const_pointer = const element_type*;
        using atomic_pointer = std::atomic<pointer>;

    private:
        pointer _ptr;

        inline static pointer _acquire(const atomic_pointer& Ptr) {
            return domain::template use<Slot>(Ptr);
        }
        inline static void _release() {
            domain::template release<Slot>();
        }
    public:
        constexpr guarded_ptr() noexcept : _ptr(nullptr) {}

        explicit guarded_ptr(const atomic_pointer& Ptr) :
            _ptr(_acquire(Ptr)) {}

        guarded_ptr(const guarded_ptr&) = delete;

        guarded_ptr(guarded_ptr&& other) noexcept : _ptr(other._ptr) {
            other._ptr = nullptr;
        };

        guarded_ptr& operator=(const guarded_ptr&) = delete;
        guarded_ptr& operator=(guarded_ptr&& other) noexcept {
            if (this != &other) {
                _ptr = other._ptr;
                other._ptr = nullptr;
            }
        }

        guarded_ptr& operator=(const atomic_pointer& Ptr) {
            _ptr = _acquire(Ptr);
        }

        ~guarded_ptr() {
            if (_ptr != nullptr) {
                _release();
                _ptr = nullptr;
            }
        }

        pointer operator->() { return _ptr; }
        const_pointer operator->() const { return _ptr; }
        element_type& operator*() { return *_ptr; }
        const element_type& operator*() const { return *_ptr; }
        pointer get() { return _ptr; }
        const_pointer get() const { return _ptr; }
        explicit operator bool() const noexcept {
            return _ptr != nullptr;
        }

        template <size_t Slot2>
        inline friend bool operator==(const guarded_ptr& lhs, const guarded_ptr<domain, Slot2>& rhs) {
            static_assert(
                Slot != Slot2,
                "Two hazard pointers for one slot detected!"
                "Only one hazard_ptr per slot per thread is allowed!"
            );
            return lhs.get() == rhs.get();
        }

        // releases held pointer
        void release() {
            _release();
            _ptr = nullptr;
        }
        
        // replaces held pointer
        void reset(const atomic_pointer& Ptr) {
            _ptr = _acquire(Ptr);
        }

        // retires held pointer
        void retire() {
            domain::retire(_ptr);
        }

        // releases and retires held pointer
        void release_retire() {
            _release();
            domain::retire(_ptr);
            _ptr = nullptr;
        }
    };
    template <Domain Domain> using hazard_ptr0 = guarded_ptr<Domain, 0>;
    template <Domain Domain> using hazard_ptr1 = guarded_ptr<Domain, 1>;
    template <Domain Domain> using hazard_ptr2 = guarded_ptr<Domain, 2>;
    template <Domain Domain> using hazard_ptr3 = guarded_ptr<Domain, 3>;
    template <Domain Domain> using hazard_ptr4 = guarded_ptr<Domain, 4>;
}