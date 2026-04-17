namespace lock_free::hazptr {
    template <typename T>
    inline constexpr bool is_domain = false;

    template <typename T>
    concept Domain = is_domain<T>;
}