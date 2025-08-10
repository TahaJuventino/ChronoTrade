#pragma once

#include "../core/Order.hpp"
#include "../core/AuthFlags.hpp"
#include "../utils/logger.h"
#include "../external/json.hpp"
#include "../security/FeedHashLogger.hpp"  
#include "FeedTelemetry.hpp"
#include "IFeedSource.hpp"

#include <fstream>
#include <string>
#include <atomic>
#include <mutex>
#include <queue>
#include <thread>

namespace feed {

    class FeedInjector : public IFeedSource {
        public:
            FeedInjector(const std::string& json_file,
                        FeedTelemetry& telemetry,
                        std::queue<engine::Order>& out_queue,
                        std::mutex& queue_mutex)
                : filename_(json_file), telemetry_(telemetry),
                out_queue_(out_queue), queue_mutex_(queue_mutex) {}

            void run() override {
                stop_flag_.store(false);
                reset_stream();
                std::string line;
                while (!stop_flag_.load() && std::getline(stream_, line)) {
                    engine::Order order(1.0, 1.0, 1'725'000'000);
                    int delay = 0;
                    engine::AuthFlags auth = engine::AuthFlags::TRUSTED;
                    std::string tag = "";

                    if (!parse_json_line(line, order, auth, delay, tag)) {
                        telemetry_.anomalies++;
                        continue;
                    }

                    if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));

                    std::string original_hash = security::FeedHashLogger::compute_sha256(line);
                    std::string parsed_hash = security::FeedHashLogger::compute_sha256(order.to_string());

                    if (original_hash != parsed_hash) {
                        security::FeedHashLogger::log_anomaly(original_hash, parsed_hash, source_tag());
                    } else {
                        security::FeedHashLogger::log_packet(line, original_hash, source_tag());
                    }

                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        out_queue_.push(order);
                    }

                    telemetry_.orders_received++;

                    SAFE_LOG(INFO) << "[Injected Order] tag=" << tag
                                << " auth=" << to_string(auth)
                                << " → " << order;
                }
            }

            void stop() override {
                stop_flag_.store(true);
            }

            std::string source_tag() const override {
                return "SRC_INJECTOR";
            }

            void reset_stream() override {
                stream_.close();
                stream_.clear();
                stream_.open(filename_);
                if (!stream_.is_open())
                    throw std::runtime_error("Failed to open inject file: " + filename_);
            }

            void reset_for_restart() override {
                IFeedSource::reset_for_restart();
                stop_flag_.store(false);
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

        private:
            bool parse_json_line(const std::string& line,
                                engine::Order& order,
                                engine::AuthFlags& auth,
                                int& delay,
                                std::string& tag) {
                try {
                    auto j = nlohmann::json::parse(line);

                    double price = j.at("price");
                    double amount = j.at("amount");
                    int64_t ts = j.at("timestamp");

                    // Optional fields
                    tag = j.contains("tag") ? j.at("tag").get<std::string>() : "";
                    delay = j.contains("delay_ms") ? j.at("delay_ms").get<int>() : 0;
                    std::string auth_str = j.value("auth", "TRUSTED");

                    auth = engine::from_string(auth_str);
                    order = engine::Order(price, amount, ts);

                    return true;
                } catch (...) {
                    SAFE_LOG(WARN) << "[Injector] Malformed or missing field: " << line;
                    return false;
                }
            }

            std::ifstream stream_;
            std::string filename_;
            FeedTelemetry& telemetry_;
            std::queue<engine::Order>& out_queue_;
            std::mutex& queue_mutex_;
            std::atomic<bool> stop_flag_;
        };

} // namespace feed