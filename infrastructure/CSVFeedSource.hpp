#pragma once

#include "interfaces/IFeedSource.hpp"
#include "observability/FeedTelemetry.hpp"
#include "core/Order.hpp"
#include "security/FeedHashLogger.hpp"

#include <string>
#include <atomic>
#include <fstream>
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <queue>
#include <mutex>
#include <cmath>
#include <vector>

namespace feed {
 
    class CSVFeedSource : public IFeedSource {
        public:
            CSVFeedSource(const std::string& filename,
                        int tick_delay_ms,
                        FeedTelemetry& telemetry,
                        std::queue<engine::Order>& out_queue,
                        FeedStamina& stamina,
                        std::mutex& queue_mutex)
                : filename_(filename),
                tick_delay_ms_(tick_delay_ms),
                telemetry_(telemetry),
                stamina_(stamina),
                out_queue_(out_queue),
                queue_mutex_(queue_mutex),
                stop_flag_(false) {}

            void run() override {
                stop_flag_.store(false, std::memory_order_relaxed);
                auto start = std::chrono::steady_clock::now();

                reset_stream();

                std::string line;
                uint64_t local_count = 0;
                int64_t last_ts = 0;

                while (!stop_flag_.load(std::memory_order_acquire) && std::getline(stream_, line)) {
                    auto original_hash = security::FeedHashLogger::compute_sha256(line);

                    if (tick_delay_ms_ > 0)
                        std::this_thread::sleep_for(std::chrono::milliseconds(tick_delay_ms_));

                    double price, amount;
                    int64_t ts;
                    if (!parse_line(line, price, amount, ts)) {
                        telemetry_.anomalies++;
                        continue;
                    }

                    if (ts <= last_ts) {
                        telemetry_.anomalies++;
                        continue;
                    }
                    last_ts = ts;

                    try {
                        engine::Order order(price, amount, ts);
                        auto parsed_hash = security::FeedHashLogger::compute_sha256(order.to_string());

                        if (parsed_hash != original_hash)
                            security::FeedHashLogger::log_anomaly(original_hash, parsed_hash, source_tag());
                        else
                            security::FeedHashLogger::log_packet(line, original_hash, source_tag());

                        {
                            std::lock_guard<std::mutex> lock(queue_mutex_);
                            out_queue_.push(order);
                        }
                        telemetry_.orders_received++;
                        local_count++;
                    } catch (...) {
                        telemetry_.anomalies++;
                    }
                }

                stream_.close();

                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();

                stamina_.successful_restarts++;
                stamina_.recovery_latency_ms = duration_ms;
                stamina_.live_processing_rate = duration_ms > 0
                    ? static_cast<int>(local_count * 1000 / duration_ms)
                    : 0;
            }

            void stop() override {
                stop_flag_.store(true, std::memory_order_release);
            }

            std::string source_tag() const override {
                return "SRC_CSV_" + filename_;
            }

            void reset_stream() override {
                stream_.close();
                stream_.clear();
                stream_.open(filename_);

                if (!stream_.is_open())
                    throw std::runtime_error("Failed to open CSV file: " + filename_);
            }

            void reset_for_restart() override {
                IFeedSource::reset_for_restart();
                stop_flag_.store(false, std::memory_order_relaxed);
                reset_stream();
            }

            bool has_telemetry() const override { 
                return true; 
            }

            FeedTelemetry& telemetry() override { 
                return telemetry_; 
            }

            const FeedTelemetry& telemetry() const override { 
                return telemetry_; 
            }

            FeedStamina& stamina() { 
                return stamina_; 
            }

            const FeedStamina& stamina() const { 
                return stamina_; 
            }

        private:
            int tick_delay_ms_;
            std::ifstream stream_;
            std::string filename_;
            FeedTelemetry& telemetry_;
            FeedStamina& stamina_;
            std::queue<engine::Order>& out_queue_;
            std::mutex& queue_mutex_;
            std::atomic<bool> stop_flag_;

            bool parse_line(const std::string& line, double& price, double& amount, int64_t& timestamp) {
                // ASCII check moved here
                for (unsigned char c : line) {
                    if (c < 32 || c > 126) return false;
                }

                std::istringstream ss(line);
                std::string token;
                std::vector<std::string> fields;

                while (std::getline(ss, token, ',')) {
                    fields.push_back(token);
                }

                if (fields.size() != 3) return false;

                try {
                    size_t end;
                    price = std::stod(fields[0], &end);
                    if (end != fields[0].size() || !std::isfinite(price) || price <= 0.0) return false;

                    amount = std::stod(fields[1], &end);
                    if (end != fields[1].size() || !std::isfinite(amount) || amount <= 0.0) return false;

                    timestamp = std::stoll(fields[2], &end);
                    if (end != fields[2].size() || timestamp <= 0) return false;

                    return true;
                } catch (...) {
                    return false;
                }
            }

            // Check if all characters in the line are printable ASCII
            bool is_ascii_printable(const std::string& line) {
                for (unsigned char c : line) {
                    if (c < 32 || c > 126) return false;
                }
                return true;
            }
        };

} // namespace feed
