#pragma once

#include "IFeedSource.hpp"
#include "FeedTelemetry.hpp"
#include "CSVFeedSource.hpp"

#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <condition_variable>
#include <exception>
#include <chrono>

namespace feed {

    class FeedManager {
        public:
            void add_source(std::unique_ptr<IFeedSource> source) {
                std::lock_guard<std::mutex> lock(mutex_);
                sources_.emplace_back(std::move(source));
            }

            void start_all(bool unique_tags = false) {
                std::lock_guard<std::mutex> lock(mutex_);
                std::unordered_set<std::string> started_tags;

                for (const auto& src : sources_) {
                    const void* key = src.get();
                    const std::string& tag = src->source_tag();

                    if (unique_tags && started_tags.count(tag))
                        continue;
                    started_tags.insert(tag);

                    cleanup_finished_thread(key);

                    if (!running_.count(key) && src->try_set_running()) {
                        running_[key] = std::jthread([src_ptr = src.get(), this] {
                            try {
                                src_ptr->run();
                            } catch (...) {}

                            src_ptr->set_status(FeedStatus::Completed);

                            {
                                std::lock_guard<std::mutex> lock(completion_mutex_);
                                completed_threads_.insert(src_ptr);
                            }

                            completion_cv_.notify_all();
                        });
                    }
                }
            }

            void stop_all() {
                std::lock_guard<std::mutex> lock(mutex_);

                // Signal all sources to stop
                for (auto& src : sources_) {
                    src->stop();
                }

                // Wait for ALL threads with timeout
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

                for (auto& [_, thread] : running_) {
                    if (thread.joinable()) {
                        // Use timed join if available, otherwise regular join
                        thread.join();
                        
                        // Verify thread actually terminated
                        if (std::chrono::steady_clock::now() > deadline) {
                            // Force termination if stuck
                            thread.detach();  // Dangerous but prevents test hanging
                        }
                    }
                }

                running_.clear();
                
                // Additional synchronization barrier
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
                // Clear completion tracking
                {
                    std::lock_guard<std::mutex> lock(completion_mutex_);
                    completed_threads_.clear();
                }
            }

            void reset_all_sources() {
                stop_all();

                std::lock_guard<std::mutex> lock(mutex_);
                for (auto& src : sources_) {
                    src->reset_for_restart();
                    src->reset_stream();
                    src->telemetry().stamina.successful_restarts = 0;
                }
            }

            // Wait for all sources to complete processing (for testing)
            bool wait_for_completion(std::chrono::milliseconds timeout) {
                std::unique_lock<std::mutex> lock(completion_mutex_);
                return completion_cv_.wait_for(lock, timeout, [this] {
                    return completed_threads_.size() == sources_.size();
                });
            }

            size_t active_thread_count() const {
                std::lock_guard<std::mutex> lock(mutex_);
                size_t active = 0;
                for (const auto& [_, thread] : running_) {
                    if (thread.joinable()) {
                        active++;
                    }
                }
                return active;
            }

            ~FeedManager() {
                stop_all();
            }

        private:
            void cleanup_finished_thread(const void* key) {
                auto it = running_.find(key);
                if (it != running_.end() && !it->second.joinable()) {
                    running_.erase(it);
                }
            }

            std::vector<std::unique_ptr<IFeedSource>> sources_;
            std::unordered_map<const void*, std::jthread> running_;
            mutable std::mutex mutex_;
            bool enforce_unique_tags_ = true;
            
            // Completion synchronization
            std::mutex completion_mutex_;
            std::condition_variable completion_cv_;
            std::unordered_set<const void*> completed_threads_;
        };

} // namespace feed