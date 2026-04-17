#include <algorithm>
#include <iostream>
#include <mutex>
#include <vector>
#include <thread>
#include <lock_free/hazard_queue.hpp>

template <size_t Threads = 16, size_t Iterations = 100000>
void hazard_queue_test() {
    struct thread_tagged_count {
        size_t thread_id;
        size_t count;
    };

    using hazard_queue = lock_free::hazard_queue<thread_tagged_count, Threads>;
    hazard_queue hq;
    static std::mutex cout_mtx;

    auto producer = [&hq](size_t thread_id) {
        for (size_t i = 1; i < Iterations; ++i) {
            hq.push_back({ thread_id, i });
        }
    };
    auto consumer = [&hq]() {
        std::array<size_t, Threads> data;
        std::fill_n(data.data(), Threads, 0);
        size_t violations = 0;
        for (size_t i = 0; i < Iterations; ++i) {
            auto res = hq.pop_front();
            if (res.has_value()) {
                auto& value = res.value();
                if (data[value.thread_id] >= value.count) {
                    ++violations;
                    std::lock_guard<std::mutex> lck(cout_mtx);
                    std::cout << "violated FIFO at: " << value.count << ' ' << value.thread_id << '\n';
                }
            }
            hq.free_garbage_memory();
        }
        {
            std::lock_guard<std::mutex> lck(cout_mtx);
            std::cout << violations << " violations\n";
        }
    };
    static_assert(hazard_queue::is_always_lock_free, "Hazard queue not lock free!");
    assert(hq.is_lock_free());

    std::vector<std::thread> threads;
    threads.reserve(Threads);
    for(size_t i = 0; i < threads.capacity(); i += 2) {
        threads.emplace_back(producer, i);
        threads.emplace_back(consumer);
    }
    for (auto& th : threads) th.join();
}

int main(int, char**) {
    hazard_queue_test();
    return 0;
}
