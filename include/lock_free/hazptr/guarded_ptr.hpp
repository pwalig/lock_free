#pragma once
#include <cassert>
#include <lock_free/hazptr/domain_traits.hpp>

namespace lock_free::hazptr {
    // Smart pointer that prevents assigned pointer from being freed by other threads.
    // Uses hazard pointer mechanism (https://en.wikipedia.org/wiki/Hazard_pointer).
    // Wraps `Domain` 's `use`, `release` and `retire` static methods.
    template <Domain HazardDomain>
    struct guarded_ptr {
        using domain = HazardDomain;
        using domain_traits = domain_traits<domain>;
        using element_type = domain_traits::value_type;
        using pointer = domain_traits::pointer;
        using const_pointer = domain_traits::const_pointer;
        using atomic_pointer = domain_traits::atomic_pointer;

    private:
        pointer _ptr;
#if defined(_MSC_VER) && !defined(__llvm__) && !defined(__INTEL_COMPILER)
        [[msvc::no_unique_address]] domain _domain;
#else
        [[no_unique_address]] domain _domain;
#endif

    public:
        constexpr guarded_ptr(
            const domain& Domain = domain()
        ) noexcept : _ptr(nullptr), _domain(Domain) {}

        explicit guarded_ptr(
            const atomic_pointer& Ptr,
            const domain& Domain = domain()
        ) : _ptr(Domain.protect(Ptr)), _domain(Domain) {}

        guarded_ptr(const guarded_ptr&) = delete;

        constexpr guarded_ptr(
            guarded_ptr&& other
        ) noexcept : _ptr(other._ptr), _domain(other._domain) {
            other._ptr = nullptr;
        };

        guarded_ptr& operator=(const guarded_ptr&) = delete;
        constexpr guarded_ptr& operator=(guarded_ptr&& other) noexcept {
            if (this != &other) {
                _ptr = other._ptr;
                _domain = other._domain;
                other._ptr = nullptr;
            }
        }

        guarded_ptr& operator=(const atomic_pointer& Ptr) {
            if constexpr (!domain_traits::override_on_protect_v) {
                _domain.release(_ptr);
            }
            _ptr = _domain.protect(Ptr);
        }

        ~guarded_ptr() {
            if (_ptr != nullptr) {
                _domain.release(_ptr);
                _ptr = nullptr;
            }
        }

        pointer operator->() { return _ptr; }
        const_pointer operator->() const { return _ptr; }
        element_type& operator*() { return *_ptr; }
        const element_type& operator*() const { return *_ptr; }
        pointer get() { return _ptr; }
        const_pointer get() const { return _ptr; }

        // Available to enable compare_exchange,
        // but be careful as compare_exchange will replace internal pointer.
        // `Reset` will have to be called afterwards.
        operator pointer&() { return _ptr; }
        operator const_pointer&() const { return _ptr; }

        explicit operator bool() const noexcept {
            return _ptr != nullptr;
        }

        inline friend bool operator==(const guarded_ptr& lhs, const guarded_ptr& rhs) {
            return lhs.get() == rhs.get();
        }

        // releases held pointer
        void release() {
            _domain.release(_ptr);
            _ptr = nullptr;
        }
        
        // replaces held pointer
        void reset(const atomic_pointer& Ptr) {
            _ptr = _domain.protect(Ptr);
        }

        // retires held pointer
        void retire() {
            _domain.retire(_ptr);
        }

        // releases and retires held pointer
        void release_retire() {
            _domain.release(_ptr);
            _domain.retire(_ptr);
            _ptr = nullptr;
        }
    };
}