#pragma once

#include "Order.hpp"
#include "EngineConfig.hpp"
#include "../utils/ArenaAllocator.hpp"
#include "../security/SecurityAwareLogger.hpp"
#include "../utils/SimdSort.hpp"

#include <vector>
#include <mutex>
#include <algorithm>
#include <unordered_set>
#include <chrono>
#include <memory>


namespace engine {

    class OrderBook {
        public:
            explicit OrderBook(utils::ArenaAllocator* a = nullptr, std::size_t capacity = 1024)
                : arena(a), max_orders(capacity) {
                if (arena) {
                    std::size_t total_bytes = sizeof(Order) * max_orders;
                    try {
                        arena_buffer = static_cast<Order*>(arena->allocate(total_bytes, alignof(Order)));
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Info,
                            "[OrderBook] ArenaAllocator enabled with {} slots.",
                            max_orders);
                    } catch (const std::bad_alloc& e) {
                        arena = nullptr;
                        arena_buffer = nullptr;
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Error,
                            "[OrderBook Arena Init Failed] Falling back. Reason: {}",
                            e.what());
                    } catch (const std::exception& ex) {
                        arena = nullptr;
                        arena_buffer = nullptr;
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Error,
                            "[OrderBook Arena Init Failed] Exception: {}",
                            ex.what());
                    } catch (...) {
                        arena = nullptr;
                        arena_buffer = nullptr;
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Error,
                            "[OrderBook Arena Init Failed] Unknown exception. Fallback activated.");
                    }
                } else {
                    security::SecurityAwareLogger::instance().log(
                        security::SecurityAwareLogger::Level::Warn,
                        "[OrderBook] ArenaAllocator not used. Fallback to std::vector.");
                }
            }

            void insert(const Order& o) {
                std::lock_guard<std::mutex> lock(mtx);
                
                if (seen_timestamps.contains(o.timestamp)) {
                    return;  // duplicate ts → reject
                }
                seen_timestamps.insert(o.timestamp);

                if (arena) {
                    if (arena_count >= max_orders) {
                        ++arena_failures;
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Warn,
                            "[Arena Overflow] Full ({}/{})",
                            arena_count, max_orders);
                        return;
                    }

                    try {
                        Order* target = &arena_buffer[arena_count++];
                        new (target) Order(o.price, o.amount, o.timestamp);  // placement new
                    } catch (...) {
                        ++arena_failures;
                        security::SecurityAwareLogger::instance().log(
                            security::SecurityAwareLogger::Level::Warn,
                            "[Arena Memory Exhausted]");
                        return;
                    }
                } else {
                    fallback_orders.push_back(o);
                }
            }

            // Additional method to get total capacity
            std::size_t capacity() const {
                std::lock_guard<std::mutex> lock(mtx);
                return arena ? max_orders : fallback_orders.capacity();
            }

            // Method to check if arena is full
            bool is_arena_full() const {
                std::lock_guard<std::mutex> lock(mtx);
                return arena && (arena_count >= max_orders);
            }

            std::size_t size() const {
                std::lock_guard<std::mutex> lock(mtx);
                return arena ? arena_count : fallback_orders.size();
            }

            std::vector<Order> snapshot() const {
                std::lock_guard<std::mutex> lock(mtx);
                if (arena)
                    return std::vector<Order>(arena_buffer, arena_buffer + arena_count);
                return fallback_orders;
            }

            void sort_by_price_desc() {
                std::lock_guard<std::mutex> lock(mtx);
                if (arena) {
                    std::vector<Order> tmp(arena_buffer, arena_buffer + arena_count);
                    utils::simd_sort_desc(tmp);
                    std::copy(tmp.begin(), tmp.end(), arena_buffer);
                } else {
                    utils::simd_sort_desc(fallback_orders);
                }
            }

            void bind_arena(utils::ArenaAllocator* a) {
                arena = a;
            }

            int failed_arena_inserts() const {
                return arena_failures;
            }

        private:
            mutable std::mutex mtx;
            std::unordered_set<std::int64_t> seen_timestamps; // to track unique timestamps
            
            // Arena mode
            utils::ArenaAllocator* arena = nullptr;
            Order* arena_buffer = nullptr;
            std::size_t arena_count = 0;
            std::size_t max_orders = 0;
            int arena_failures = 0;

            // Fallback
            std::vector<Order> fallback_orders;
        };

} // namespace engine