#pragma once
#include <thread>
#include <atomic>

namespace lock_free {
    template <typename T, size_t MaxThreads>
    struct shared_thread_local {
        using value_type = T;

        struct data {
            T _data;
            std::atomic<std::thread::id> thread_id;

            data() : _data(), thread_id(std::thread::id()) {}

            T* operator->() { return &_data; }
            const T* operator->() const { return &_data; }
            T& operator*() { return _data; }
            const T& operator*() const { return _data; }
            T& value() { return _data; }
            const T& value() const { return _data; }
        };

        inline static std::array<data, MaxThreads> thread_slots;

        struct owner {
            data* _data;

            owner() : _data(nullptr) {
                while (_data == nullptr) {
                    for (auto& slot : thread_slots) {
                        std::thread::id old_id = std::thread::id();
                        if (slot.thread_id.compare_exchange_strong(
                            old_id,
                            std::this_thread::get_id()
                        )) {
                            _data = &slot;
                            break;
                        }
                    }
                }
            }

            ~owner() {
                _data->thread_id.store(std::thread::id());
            }
        };

        inline static T& get() {
            thread_local owner _owner;
            return _owner._data->_data;
        }

        inline operator T&() {
            return get();
        }
    };
}