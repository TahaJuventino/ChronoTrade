#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace engine::threads {

    class ThreadPool {
        public:
            explicit ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
            ~ThreadPool();

            void submit(std::function<void()> task);

        private:
            std::vector<std::thread> workers;
            std::queue<std::function<void()>> tasks;

            std::mutex queue_mutex;
            std::condition_variable condition;
            std::atomic<bool> stop;

            void worker_loop();
        };

        inline ThreadPool::ThreadPool(size_t num_threads) : stop(false) {
            for (size_t i = 0; i < num_threads; ++i)
                workers.emplace_back(&ThreadPool::worker_loop, this);
        }

        inline ThreadPool::~ThreadPool() {
            stop = true;
            condition.notify_all();
            for (auto& t : workers)
                if (t.joinable()) t.join();
        }

        inline void ThreadPool::submit(std::function<void()> task) {
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                tasks.push(std::move(task));
            }
            condition.notify_one();
        }

        inline void ThreadPool::worker_loop() {
            while (!stop) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    condition.wait(lock, [this] { return stop || !tasks.empty(); });
                    if (stop && tasks.empty()) return;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task();
            }
        }

} // namespace engine::threads
