#include <algorithm>
#include <iostream>
#include <mutex>
#include <new>
#include <vector>
#include <thread>
#include <lock_free/hazard_queue.hpp>
#include <msnb_queue.hpp>
#include <lock_free/retire_allocator.hpp>

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

template <size_t Threads = 16, size_t Iterations = 100000>
void msnb_queue_test() {
    struct thread_tagged_count {
        size_t thread_id;
        size_t count;
    };

    static constexpr size_t Alignment = std::hardware_destructive_interference_size;
    using allocator = lock_free::retire_allocator<thread_tagged_count, Threads>;
    // using allocator = std::allocator<thread_tagged_count>;
    using msnb_queue = msnb_queue<thread_tagged_count, Alignment, allocator>;
    msnb_queue hq;
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
        }
        {
            std::lock_guard<std::mutex> lck(cout_mtx);
            std::cout << violations << " violations\n";
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(Threads);
    for(size_t i = 0; i < threads.capacity(); i += 2) {
        threads.emplace_back(producer, i);
        threads.emplace_back(consumer);
    }
    for (auto& th : threads) th.join();
    using node_allocator = lock_free::retire_allocator<typename msnb_queue::node_type, Threads>;
    node_allocator::cleanup();
}

int main(int, char**) {
    std::cout << "Hazard\n";
    hazard_queue_test();
    std::cout << "Michael & Scott\n";
    msnb_queue_test();
    return 0;
}
