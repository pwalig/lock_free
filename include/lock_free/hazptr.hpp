#pragma once
#include <atomic>
namespace lock_free::hazptr {
    template <typename T>
    inline bool empty(const std::atomic<T*>& hazard_pointer) {
        return hazard_pointer.load() == nullptr;
    }
    template <typename T>
    inline T* protect(
        const std::atomic<T*>& hazard_pointer,
        const std::atomic<T*>& shared_pointer
    ) {
        T* ptr;
        do {
            ptr = shared_pointer.load();
            hazard_pointer.store(ptr);
        } while (ptr != shared_pointer.load());
        return ptr;
    }
    template <typename T>
    inline bool try_protect(
        const std::atomic<T*>& hazard_pointer,
        T*& ptr,
        const std::atomic<T*>& shared_pointer
    ) {
        ptr = shared_pointer.load();
        hazard_pointer.store(ptr);
        if (ptr != shared_pointer.load()) {
            hazard_pointer.store(nullptr);
            return false;
        }
        else return true;
    }
    template <typename T>
    inline void release(
        const std::atomic<T*>& hazard_pointer
    ) {
        hazard_pointer.store(nullptr);
    }
}