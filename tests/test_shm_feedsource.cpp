#include <gtest/gtest.h>
#include <fstream>
#include <thread>
#include <queue>
#include <mutex>
#include <chrono>
#include <cstring>
#include <random>
#include <algorithm>
#include "../feed/SharedMemoryFeedSource.hpp"
#include "../security/FeedHashLogger.hpp"

using namespace feed;
using namespace engine;

namespace {

    class SHMTestFixture {
        private:
            std::string shm_name_;
            int fd_;
            RingBuffer* ring_;
            
        public:
            explicit SHMTestFixture(const std::string& name) 
                : shm_name_(name), fd_(-1), ring_(nullptr) {
                cleanup();
                create_shm();
            }
            
            ~SHMTestFixture() {
                cleanup();
            }
            
            void create_shm() {
                fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR, 0666);
                if (fd_ < 0) throw std::runtime_error("Failed to create test SHM: " + shm_name_);
                
                if (ftruncate(fd_, sizeof(RingBuffer)) != 0) {
                    close(fd_);
                    throw std::runtime_error("ftruncate failed for: " + shm_name_);
                }
                
                void* ptr = mmap(nullptr, sizeof(RingBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
                if (ptr == MAP_FAILED) {
                    close(fd_);
                    throw std::runtime_error("mmap failed for: " + shm_name_);
                }
                
                ring_ = static_cast<RingBuffer*>(ptr);
                // Initialize atomics explicitly
                ring_->head.store(0, std::memory_order_release);
                ring_->tail.store(0, std::memory_order_release);
                
                // Zero out packet array for deterministic behavior
                std::memset(ring_->packets, 0, sizeof(ring_->packets));
            }
            
            void inject_packets(const std::vector<std::string>& payloads) {
                if (!ring_) throw std::runtime_error("Ring buffer not initialized");
                
                uint32_t head = ring_->head.load(std::memory_order_acquire);
                
                for (const auto& payload : payloads) {
                    if (payload.size() >= kPacketSize) {
                        throw std::runtime_error("Payload exceeds packet size: " + std::to_string(payload.size()));
                    }
                    
                    // Check for buffer overflow before writing
                    uint32_t next_head = (head + 1) % kMaxPackets;
                    if (next_head == ring_->tail.load(std::memory_order_acquire)) {
                        throw std::runtime_error("Ring buffer full - cannot inject more packets");
                    }
                    
                    Packet& pkt = ring_->packets[head % kMaxPackets];
                    std::memset(pkt.data, 0, kPacketSize);  // Clear previous data
                    std::memcpy(pkt.data, payload.c_str(), payload.size());
                    pkt.len = static_cast<uint16_t>(payload.size());
                    
                    head = next_head;
                }
                
                // Commit all writes atomically
                ring_->head.store(head, std::memory_order_release);
            }
            
            void cleanup() {
                if (ring_) {
                    munmap(ring_, sizeof(RingBuffer));
                    ring_ = nullptr;
                }
                if (fd_ != -1) {
                    close(fd_);
                    fd_ = -1;
                }
                shm_unlink(shm_name_.c_str());
            }
            
            const std::string& name() const { 
                return shm_name_; 
            }

            // For testing ring buffer state
            uint32_t get_head() const { 
                return ring_->head.load(std::memory_order_acquire); 
            }
            
            uint32_t get_tail() const { 
                return ring_->tail.load(std::memory_order_acquire); 
            }
        };

        // Generate deterministic but varied test data
        std::vector<std::string> generate_csv_packets(size_t count, uint64_t base_timestamp = 1725000000) {
            std::vector<std::string> packets;
            packets.reserve(count);
            
            for (size_t i = 0; i < count; ++i) {
                double price = 100.0 + (i * 0.1);
                double amount = 1.0 + (i * 0.01);
                uint64_t timestamp = base_timestamp + i;
                
                std::string packet = std::to_string(price) + "," + 
                                std::to_string(amount) + "," + 
                                std::to_string(timestamp);
                packets.push_back(packet);
            }
            return packets;
        }

        // Generate malicious/edge case packets for robustness testing
        std::vector<std::string> generate_attack_packets() {
            return {
                "101.5,2.0,1725000001",                    // Valid #1
                "102.0,1.5,1725000002",                    // Valid #2  
                "103.5,1.0,1725000003",                    // Valid #3
                "104.0,2.5,1725000004",                    // Valid #4 - but check if negatives/zeros are actually valid in your Order constructor
                "",                                        // Empty packet
                "invalid,csv,format,extra,field",          // Too many fields
                "not_a_number,2.0,1725000005",            // Invalid price
                "101.5,not_a_number,1725000006",          // Invalid amount
                "101.5,2.0,not_a_timestamp",              // Invalid timestamp
                std::string(kPacketSize - 1, 'A'),        // Maximum size packet
                "1e308,1e308,1725000007",                 // Overflow values - likely causing stod() exception
                "101.5,2.0",                              // Missing field
                "101.5,2.0,1725000009,extra"              // Extra trailing data
            };
        }

}  // namespace

class SharedMemoryFeedSourceTest : public ::testing::Test {
    protected:
        void TearDown() override {
            // Aggressive cleanup to prevent test pollution
            std::vector<std::string> test_shm_names = {
                "/test_shm_basic",
                "/test_shm_malformed", 
                "/test_shm_binary",
                "/test_shm_overflow",
                "/test_shm_stress",
                "/test_shm_timing",
                "/test_shm_security"
            };
            
            for (const auto& name : test_shm_names) {
                shm_unlink(name.c_str());
            }
        }
};

TEST_F(SharedMemoryFeedSourceTest, BasicFunctionalityWithCorrectSHMUsage) {
    SHMTestFixture fixture("/test_shm_basic");
    std::vector<std::string> csv_packets = generate_csv_packets(3);
    fixture.inject_packets(csv_packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    std::thread consumer_thread([&]() { feed_source.run(); });
    
    // Allow sufficient processing time
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    feed_source.stop();
    consumer_thread.join();

    EXPECT_EQ(telemetry.orders_received.load(), 3);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
    EXPECT_EQ(order_queue.size(), 3);
    
    // Verify head/tail pointers advanced correctly
    EXPECT_EQ(fixture.get_head(), 3);
    EXPECT_EQ(fixture.get_tail(), 3);
}

TEST_F(SharedMemoryFeedSourceTest, MalformedPacketHandlingAndAnomalyTracking) {
    SHMTestFixture fixture("/test_shm_malformed");
    std::vector<std::string> attack_packets = generate_attack_packets();
    fixture.inject_packets(attack_packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    std::thread consumer_thread([&]() { feed_source.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    feed_source.stop();
    consumer_thread.join();

    // Should process valid packets and detect multiple anomalies
    EXPECT_EQ(telemetry.orders_received.load(), 4);   // Exactly 4 valid packets
    EXPECT_EQ(telemetry.anomalies.load(), 9);         // Exactly 9 invalid packets (11 total - 4 valid + 2 that might parse)
    
    // Verify system didn't crash and maintains state consistency
    EXPECT_EQ(fixture.get_head(), attack_packets.size());
    EXPECT_EQ(fixture.get_tail(), attack_packets.size());
}

TEST_F(SharedMemoryFeedSourceTest, BinaryDataDetectionAndFiltering) {
    SHMTestFixture fixture("/test_shm_binary");
    
    std::vector<std::string> mixed_packets = {
        "101.5,2.0,1725000001",                      // Valid CSV
        std::string("\x01\x02\x03\xFF\x00", 5),     // Binary with null
        std::string("\x80\x81\x82\x83", 4),         // High-bit binary
        "102.0,1.5,1725000002",                      // Valid CSV
        std::string("\x1F\x7F", 2),                  // Control characters
        "103.5,1.0,1725000003"                       // Valid CSV
    };
    
    fixture.inject_packets(mixed_packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    std::thread consumer_thread([&]() { feed_source.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    feed_source.stop();
    consumer_thread.join();

    EXPECT_EQ(telemetry.orders_received.load(), 3);  // Only valid CSV packets
    EXPECT_EQ(telemetry.anomalies.load(), 3);        // Three binary packets detected
    EXPECT_EQ(order_queue.size(), 3);
}

TEST_F(SharedMemoryFeedSourceTest, RingBufferOverflowProtection) {
    SHMTestFixture fixture("/test_shm_overflow");
    
    // Try to inject more packets than ring buffer can hold
    std::vector<std::string> overflow_packets = generate_csv_packets(kMaxPackets + 100);
    
    // Should throw or handle gracefully - inject only what fits
    EXPECT_THROW(fixture.inject_packets(overflow_packets), std::runtime_error);
    
    // Test with exactly maximum capacity
    std::vector<std::string> max_packets = generate_csv_packets(kMaxPackets - 1);
    EXPECT_NO_THROW(fixture.inject_packets(max_packets));
}

TEST_F(SharedMemoryFeedSourceTest, HighThroughputStressTest) {
    SHMTestFixture fixture("/test_shm_stress");
    
    // Generate large dataset with varied timestamps to test performance
    std::vector<std::string> stress_packets = generate_csv_packets(500, 1725000000);
    fixture.inject_packets(stress_packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 8192, telemetry, order_queue, queue_mutex);

    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::thread consumer_thread([&]() { feed_source.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    feed_source.stop();
    consumer_thread.join();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    EXPECT_EQ(telemetry.orders_received.load(), 500);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
    EXPECT_EQ(order_queue.size(), 500);
    
    // Performance assertion - allow margin for OS scheduling variance
    EXPECT_LT(duration.count(), 500000);  // Less than 500ms - realistic under load
    
    // Verify throughput rate meets minimum requirements
    double packets_per_ms = static_cast<double>(telemetry.orders_received.load()) / 
                           (duration.count() / 1000.0);
    EXPECT_GT(packets_per_ms, 1.0);  // Minimum 1 packet per millisecond
}

TEST_F(SharedMemoryFeedSourceTest, ConcurrentProducerConsumerSafety) {
    SHMTestFixture fixture("/test_shm_timing");
    
    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    std::thread consumer_thread([&]() { feed_source.run(); });
    
    // Simulate live producer injecting packets while consumer runs
    std::thread producer_thread([&]() {
        for (int batch = 0; batch < 5; ++batch) {
            std::vector<std::string> batch_packets = generate_csv_packets(10, 1725000000 + batch * 10);
            
            try {
                fixture.inject_packets(batch_packets);
            } catch (const std::runtime_error& e) {
                // Expected when buffer fills - break cleanly
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    feed_source.stop();
    
    producer_thread.join();
    consumer_thread.join();

    // Verify no data corruption occurred during concurrent access
    EXPECT_GE(telemetry.orders_received.load(), 10);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
    EXPECT_GE(order_queue.size(), 10);
}

TEST_F(SharedMemoryFeedSourceTest, SecurityAndInputValidation) {
    SHMTestFixture fixture("/test_shm_security");
    
    // Explicitly test each packet type individually first
    std::vector<std::string> known_valid_packets = {
        "101.5,2.0,1725000001",
        "102.0,1.5,1725000002", 
        "103.5,1.0,1725000003",
        "104.0,2.5,1725000004"
    };
    
    std::vector<std::string> known_invalid_packets = {
        "",                                           // Empty
        "invalid,format",                            // Wrong field count
        "abc,2.0,1725000005",                       // Non-numeric price
        "101.5,xyz,1725000006",                     // Non-numeric amount
        "101.5,2.0,abc",                            // Non-numeric timestamp
        std::string(kPacketSize - 10, 'X'),         // Oversized packet
        "101.5,2.0"                                 // Missing timestamp
    };
    
    // Combine for mixed testing
    std::vector<std::string> security_test_packets = known_valid_packets;
    security_test_packets.insert(security_test_packets.end(), 
                                known_invalid_packets.begin(), 
                                known_invalid_packets.end());
    
    fixture.inject_packets(security_test_packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    std::thread consumer_thread([&]() { feed_source.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    feed_source.stop();
    consumer_thread.join();

    // Exact validation based on known packet counts
    EXPECT_EQ(telemetry.orders_received.load(), 4);   // Exactly 4 valid packets  
    EXPECT_EQ(telemetry.anomalies.load(), 7);         // Exactly 7 invalid packets
    EXPECT_EQ(order_queue.size(), 4);
    
    // Verify no buffer overflows or memory corruption
    EXPECT_EQ(fixture.get_head(), security_test_packets.size());
    EXPECT_EQ(fixture.get_tail(), security_test_packets.size());
}

TEST_F(SharedMemoryFeedSourceTest, EmptyAndNullPacketHandling) {
    SHMTestFixture fixture("/test_shm_empty");
    
    std::vector<std::string> edge_packets = {
        "",                      // Empty packet
        " ",                     // Whitespace only
        "\t",                    // Tab only
        "\n",                    // Newline only
        "101.5,2.0,1725000001"   // Valid packet to verify system still works
    };
    
    fixture.inject_packets(edge_packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    std::thread consumer_thread([&]() { feed_source.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    feed_source.stop();
    consumer_thread.join();

    EXPECT_EQ(telemetry.orders_received.load(), 1);  // Only valid packet processed
    EXPECT_EQ(telemetry.anomalies.load(), 4);        // Four problematic packets
    EXPECT_EQ(order_queue.size(), 1);
}

TEST_F(SharedMemoryFeedSourceTest, StopStartLifecycleManagement) {
    SHMTestFixture fixture("/test_shm_lifecycle");
    std::vector<std::string> packets = generate_csv_packets(100);
    fixture.inject_packets(packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    // Test multiple start/stop cycles
    for (int cycle = 0; cycle < 3; ++cycle) {
        uint64_t orders_before = telemetry.orders_received.load();
        
        std::thread consumer_thread([&]() { feed_source.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        feed_source.stop();
        consumer_thread.join();
        
        uint64_t orders_after = telemetry.orders_received.load();
        EXPECT_GE(orders_after, orders_before);  // Progress made each cycle
        
        // Reset for next iteration
        feed_source.reset_for_restart();
    }
    
    // Final verification
    EXPECT_EQ(telemetry.orders_received.load(), 100);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
}

TEST_F(SharedMemoryFeedSourceTest, TelemetryAccuracyUnderLoad) {
    SHMTestFixture fixture("/test_shm_telemetry");
    
    // Mix of valid and invalid packets with known counts
    std::vector<std::string> mixed_packets;
    
    // Add 50 valid packets
    auto valid_packets = generate_csv_packets(50);
    mixed_packets.insert(mixed_packets.end(), valid_packets.begin(), valid_packets.end());
    
    // Add 20 invalid packets
    for (int i = 0; i < 20; ++i) {
        mixed_packets.push_back("invalid_packet_" + std::to_string(i));
    }
    
    // Shuffle to test order independence
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(mixed_packets.begin(), mixed_packets.end(), g);
    
    fixture.inject_packets(mixed_packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    std::thread consumer_thread([&]() { feed_source.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    feed_source.stop();
    consumer_thread.join();

    // Verify exact telemetry accuracy
    EXPECT_EQ(telemetry.orders_received.load(), 50);
    EXPECT_EQ(telemetry.anomalies.load(), 20);
    EXPECT_EQ(order_queue.size(), 50);
    
    // Verify total packet count matches
    EXPECT_EQ(telemetry.orders_received.load() + telemetry.anomalies.load(), 70);
}

TEST_F(SharedMemoryFeedSourceTest, MemoryOrderingAndAtomicConsistency) {
    SHMTestFixture fixture("/test_shm_atomic");
    std::vector<std::string> packets = generate_csv_packets(200);
    fixture.inject_packets(packets);

    std::queue<Order> order_queue;
    std::mutex queue_mutex;
    FeedTelemetry telemetry;

    SharedMemoryFeedSource feed_source(fixture.name(), 4096, telemetry, order_queue, queue_mutex);

    // Run multiple short processing bursts to stress atomic operations
    for (int burst = 0; burst < 5; ++burst) {
        std::thread consumer_thread([&]() { feed_source.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        feed_source.stop();
        consumer_thread.join();
        
        // Verify atomics remain consistent after each burst
        uint32_t head = fixture.get_head();
        uint32_t tail = fixture.get_tail();
        EXPECT_EQ(head, tail);  // Consumer should catch up to producer
        
        feed_source.reset_for_restart();
    }

    EXPECT_EQ(telemetry.orders_received.load(), 200);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
}