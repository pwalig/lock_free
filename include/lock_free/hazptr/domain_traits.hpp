#include <atomic>
#include <memory>
#include <type_traits>

namespace lock_free::hazptr {
    template <typename T>
    inline constexpr bool is_domain = false;

    template <typename T>
    concept Domain = is_domain<T>;

    template <typename T>
    concept HasPointer = requires { typename T::pointer; };

    // Implementation of the detection idiom (negative case).
    template <typename Fallback, template <typename...> class Dependency, typename... Type>
    struct detected_or {
        using type = Fallback;
        using detected = std::false_type;
    };

    // Implementation of the detection idiom (positive case).
    template <typename Fallback, template <typename...> class Dependency, typename... Type>
    requires requires { typename Dependency<Type...>; }
    struct detected_or<Fallback, Dependency, Type...> {
        using type = Dependency<Type...>;
        using detected = std::true_type;
    };

    template <typename T, template <typename...> class Dependency, typename... Type>
    using detected_or_t = detected_or<T, Dependency, Type...>::type;

    template <Domain HazardDomain>
    struct domain_traits {
    private:
        template <Domain T> using _pointer = T::pointer;
        template <Domain T> using _const_pointer = T::const_pointer;
        template <Domain T> using _atomic_pointer = T::const_pointer;
        template <Domain T> using _override_on_protect = T::const_pointer;

    public:
        using domain = HazardDomain;
        using value_type = domain::value_type;
        using pointer = detected_or_t<value_type*, _pointer, domain>;
        using const_pointer = detected_or_t<typename std::pointer_traits<pointer>::template rebind<const value_type>, _const_pointer, domain>;
        using atomic_pointer = detected_or_t<std::atomic<pointer>, _atomic_pointer, domain>;

        using override_on_protect = detected_or_t<std::true_type, _override_on_protect, domain>;
        inline static constexpr bool override_on_protect_v = override_on_protect{};
    };

}