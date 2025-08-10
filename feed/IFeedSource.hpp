#pragma once

#include <string>
#include <atomic>
#include "feed/FeedTelemetry.hpp"

namespace feed {

    enum class FeedStatus {
        Idle,
        Running,
        Stopped,
        Completed  // Prevents automatic restart
    };

    class IFeedSource {
        public:
            virtual ~IFeedSource() = default;

            virtual void run() = 0;
            virtual void stop() = 0;
            virtual std::string source_tag() const = 0;

            // Optional stream rewind logic (CSVFeedSource overrides this)
            virtual void reset_stream() {}

            // Now marked virtual to allow override
            virtual void reset_for_restart() {
                FeedStatus expected = FeedStatus::Completed;
                status_.compare_exchange_strong(expected, FeedStatus::Idle);
                expected = FeedStatus::Stopped;
                status_.compare_exchange_strong(expected, FeedStatus::Idle);
            }

            FeedStatus status() const {
                return status_.load(std::memory_order_acquire);
            }

            void set_status(FeedStatus s) {
                status_.store(s, std::memory_order_release);
            }

            bool try_set_running() {
                FeedStatus expected = FeedStatus::Idle;
                return status_.compare_exchange_strong(expected, FeedStatus::Running);
            }

            // Optional: Override if telemetry/stamina are exposed
            virtual bool has_telemetry() const { 
                return false; 
            }

            virtual FeedTelemetry& telemetry() {
                throw std::logic_error("telemetry() not implemented");
            }

            virtual const FeedTelemetry& telemetry() const {
                throw std::logic_error("telemetry() const not implemented");
            }

        protected:
            std::atomic<FeedStatus> status_ = FeedStatus::Idle;
        };

} // namespace feed