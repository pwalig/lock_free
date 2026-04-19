#include <atomic>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <thread>
#include <unordered_set>

namespace lock_free {
    template <typename T, size_t MaxThreads>
    struct retire_allocator {
        using value_type = T;
        using pointer = T*;
        using size_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::true_type;
    private:
        struct thread_data {
            std::atomic<std::thread::id> thread_id;
            std::unordered_set<std::pair<pointer, size_type>> to_free;
#if defined(_MSC_VER) && !defined(__llvm__) && !defined(__INTEL_COMPILER)
            [[msvc::no_unique_address]] std::allocator<T> allocator;
#else
            [[no_unique_address]] std::allocator<T> allocator;
#endif
        };
        inline static std::array<thread_data, MaxThreads> thread_slots;

        class thread_data_owner {
            thread_data* _data;

        public:
            thread_data_owner() : _data(nullptr) {
                while (_data == nullptr) {
                    for (auto& slot : thread_slots) {
                        std::thread::id old_id = std::thread::id();
                        if (slot.thread_id.compare_exchange_strong(old_id, std::this_thread::get_id())) {
                            _data = &slot;
                            break;
                        }
                    }
                }
            }

            inline thread_data& value() { return *_data; }

            ~thread_data_owner() {
                _data->thread_id.store(std::thread::id());
            }
        };

        inline static thread_data& get_thread_data() {
            thread_local thread_data_owner owner;
            return owner.value();
        }


    public:
        inline pointer allocate(size_type n, const void* hint = 0) {
            get_thread_data().allocator.allocate(n, hint);
        }

        inline void deallocate(pointer* p, size_type n = 1) {
            get_thread_data().to_free.emplace(p, n);
        }

        inline static void cleanup() {
            for (thread_data& thd : thread_slots) {
                for (std::pair<pointer, size_type>& p : thd.to_free) {
                    thd.allocator.deallocate(p.first, p.second);
                }
            }
        }
    };
}