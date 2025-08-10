#pragma once

#include <atomic>

namespace feed {

    struct FeedStamina {
        std::atomic<int> successful_restarts{0};
        std::atomic<int> stalls_detected{0};
        std::atomic<int> thread_failures{0};
        std::atomic<int> recovery_latency_ms{0};   // running total or EWMA (depends on usage)
        std::atomic<int> live_processing_rate{0};  // lines/sec, should be updated externally
    };

    struct FeedTelemetry {
        std::atomic<uint64_t> orders_received{0};
        std::atomic<uint64_t> anomalies{0};
        std::atomic<uint64_t> dropped_packets{0};
        FeedStamina stamina;  // Embedded struct (not pointer)
    };

}  // namespace feed