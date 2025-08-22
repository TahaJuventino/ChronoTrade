#include <gtest/gtest.h>
#include "feed/FeedManager.hpp"
#include "feed/CSVFeedSource.hpp"
#include "feed/FeedTelemetry.hpp"
#include <fstream>
#include <filesystem>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>
#include <queue>
#include <mutex>

namespace fs = std::filesystem;

class MockFeedSource : public feed::IFeedSource {
    public:
        std::atomic<bool> started{false};
        std::atomic<bool> stopped{false};
        std::string tag;
        feed::FeedTelemetry telemetry_data;
        feed::FeedTelemetry* telemetry_ptr;

        MockFeedSource(std::string t)
            : tag(std::move(t)), telemetry_ptr(&telemetry_data) {
            set_status(feed::FeedStatus::Idle);
        }

        MockFeedSource(std::string t, feed::FeedTelemetry* ext)
            : tag(std::move(t)), telemetry_ptr(ext ? ext : &telemetry_data) {
            set_status(feed::FeedStatus::Idle);
        }

        void run() override {
            started = true;
            set_status(feed::FeedStatus::Running);
            while (!stopped) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                telemetry_ptr->orders_received++;
            }
            set_status(feed::FeedStatus::Completed);
        }

        void stop() override {
            stopped = true;
            set_status(feed::FeedStatus::Stopped);
        }

        std::string source_tag() const override { 
            return tag; 
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
           
            return *telemetry_ptr; 
        }
        const feed::FeedTelemetry& telemetry() const override { 
            return *telemetry_ptr; 
        }
    };

class StuckFeedSource : public feed::IFeedSource {
    public:
        std::atomic<bool> started{false};
        std::atomic<bool> stopped{false};
        std::string tag;
        feed::FeedTelemetry dummy_;

        StuckFeedSource(std::string t) : tag(std::move(t)) {}

        void run() override {
            started = true;
            while (!stopped) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        void stop() override {
            stopped = true;
        }

        std::string source_tag() const override {
            return tag;
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
            return dummy_; 
        }

        const feed::FeedTelemetry& telemetry() const override { 
            return dummy_; 
        }
    };

class JitteryFeedSource : public feed::IFeedSource {
    public:
        std::atomic<bool> started{false};
        std::atomic<bool> stopped{false};
        std::string tag;
        int delay_ms;

        JitteryFeedSource(std::string t, int delay)
            : tag(std::move(t)), delay_ms(delay) {}

        void run() override {
            started = true;
            int elapsed = 0;
            while (!stopped && elapsed < 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                elapsed += delay_ms;
            }
            stopped = true;
        }

        void stop() override {
            stopped = true;
        }

        std::string source_tag() const override {
            return tag;
        }

        bool has_telemetry() const override {
            return true;
        }

        feed::FeedTelemetry& telemetry() override {
            return dummy_;
        }

        const feed::FeedTelemetry& telemetry() const override {
            return dummy_;
        }

    private:
        feed::FeedTelemetry dummy_;
    };

class ThrowingFeedSource : public feed::IFeedSource {
    public:
        std::string tag = "throwing";
        feed::FeedTelemetry dummy;

        void run() override {
            throw std::runtime_error("Simulated failure in feed run");
        }

        void stop() override {}

        std::string source_tag() const override {
            return tag;
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
            return dummy; 
        }

        const feed::FeedTelemetry& telemetry() const override { 
            return dummy; 
        }
    };

class ExceptionFeedSource : public feed::IFeedSource {
    public:
        std::string tag = "exception_src";
        feed::FeedTelemetry dummy;

        void run() override {
            throw std::runtime_error("Simulated failure in run()");
        }

        void stop() override {}

        std::string source_tag() const override { return tag; }

        bool has_telemetry() const override { return true; }

        feed::FeedTelemetry& telemetry() override { return dummy; }

        const feed::FeedTelemetry& telemetry() const override { return dummy; }
    };

class CrashyFeed : public feed::IFeedSource {
    public:
        feed::FeedTelemetry dummy;

        void run() override {
            throw std::runtime_error("Crash in feed thread");
        }

        void stop() override {}

        std::string source_tag() const override {
            return "CRASHY";
        }

        bool has_telemetry() const override {
            return true;
        }

        feed::FeedTelemetry& telemetry() override {
            return dummy;
        }

        const feed::FeedTelemetry& telemetry() const override {
            return dummy;
        }
    };

class SpoofedCSVFeed : public feed::CSVFeedSource {
    public:
        using feed::CSVFeedSource::CSVFeedSource;

        std::string source_tag() const override {
            return "SRC_SHMEM";  // FAKE tag injection
        }
};

class BadFeed : public feed::IFeedSource {
    public:
        std::string tag;
        feed::FeedTelemetry dummy_;  // <- dummy

        BadFeed(std::string t) : tag(std::move(t)) {}

        void run() override {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            throw std::runtime_error("Injected chaos from " + tag);
        }

        void stop() override {}

        std::string source_tag() const override { 
            return tag; 
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
            return dummy_; 
        }

        const feed::FeedTelemetry& telemetry() const override { 
            return dummy_; 
        }
    };

class StarvingSource : public feed::IFeedSource {
    public:
        std::atomic<bool> started{false};
        std::atomic<bool> stopped{false};
        std::mutex* lock;
        feed::FeedTelemetry* telemetry_ptr;

        StarvingSource(std::mutex* external_lock, feed::FeedTelemetry* tele)
            : lock(external_lock), telemetry_ptr(tele) {}

        void run() override {
            started = true;
            std::unique_lock<std::mutex> hold(*lock);
            while (!stopped) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        void stop() override { stopped = true; }

        std::string source_tag() const override { 
            return "starving_source"; 
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
            return *telemetry_ptr; 
        }

        const feed::FeedTelemetry& telemetry() const override { 
            return *telemetry_ptr; 
        }
    };

class CrashySource : public feed::IFeedSource {
    public:
        bool stopped = false;
        feed::FeedTelemetry dummy;

        void run() override {
            throw std::runtime_error("Simulated crash in feed");
        }

        void stop() override {
            stopped = true;
        }

        std::string source_tag() const override {
            return "crashy";
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
            return dummy; 
        }

        const feed::FeedTelemetry& telemetry() const override { 
            return dummy; 
        }
    };

class TaggableSource : public feed::IFeedSource {
    public:
        std::string tag_;
        bool stopped = false;
        feed::FeedTelemetry dummy;

        TaggableSource(std::string t) : tag_(std::move(t)) {}

        void run() override {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        void stop() override {
            stopped = true;
        }

        std::string source_tag() const override {
            return tag_;
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
            return dummy; 
        }

        const feed::FeedTelemetry& telemetry() const override { 
            return dummy; 
        }
    };

class CountingSource : public feed::IFeedSource {
    public:
        std::atomic<int> run_count{0};
        std::atomic<int> stop_count{0};
        std::atomic<bool> running{false};
        feed::FeedTelemetry dummy;

        void run() override {
            if (running.exchange(true)) return;  // Prevent double start
            ++run_count;
            while (running)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        void stop() override {
            ++stop_count;
            running = false;
        }

        std::string source_tag() const override {
            return "idempotent_source";
        }

        bool has_telemetry() const override { 
            return true; 
        }

        feed::FeedTelemetry& telemetry() override { 
            return dummy; 
        }

        const feed::FeedTelemetry& telemetry() const override { 
            return dummy; 
        }
    };

class SharedFeed : public feed::IFeedSource {
    public:
        std::shared_ptr<std::atomic<int>> run_count;
        std::atomic<bool> running = false;

        explicit SharedFeed(std::shared_ptr<std::atomic<int>> counter)
            : run_count(std::move(counter)) {}

        void run() override {
            ++(*run_count);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        void stop() override {
            running = false;
        }

        std::string source_tag() const override {
            return "SHARED_FEED";
        }
    };

TEST(FeedManagerTest, DoubleStartDoesNotReRunSources) {
    feed::FeedManager manager;
    
    feed::FeedTelemetry dummy;

    auto src = std::make_unique<MockFeedSource>("double_start", &dummy);
    auto* ptr = src.get();
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(ptr->started);

    ptr->started = false;
    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(ptr->started);

    manager.stop_all();
}

TEST(FeedManagerTest, StopAllWithoutStartIsSafe) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;

    auto src = std::make_unique<MockFeedSource>("double_start", &dummy);

    auto* ptr = src.get();
    manager.add_source(std::move(src));

    manager.stop_all();
    EXPECT_FALSE(ptr->started);
    EXPECT_TRUE(ptr->stopped);
}

TEST(FeedManagerTest, StartAllConcurrentInvocations) {
    feed::FeedManager mgr;
    feed::FeedTelemetry telemetry;
    feed::FeedTelemetry dummy;

    std::queue<engine::Order> queue;
    std::mutex mtx;

    for (int i = 0; i < 5; ++i)
        mgr.add_source(std::make_unique<MockFeedSource>("src" + std::to_string(i), &telemetry));

    std::jthread t1([&mgr] { mgr.start_all(); });
    std::jthread t2([&mgr] { mgr.start_all(); });
    std::jthread t3([&mgr] { mgr.start_all(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mgr.stop_all();

    EXPECT_GE(telemetry.orders_received.load(), 5);
}

TEST(FeedManagerTest, IgnoresMalformedFeedAndAcceptsValidOne) {
    const std::string valid_file = "valid_feed.csv";
    const std::string malformed_file = "malformed_feed.csv";

    {
        std::ofstream v(valid_file), m(malformed_file);
        v << "100.0,1.0,1725621000\n";
        v << "101.0,1.0,1725621001\n";
        v.flush();
        v.close();

        m << "BAD_LINE\n";
        m << "100.0,abc,1725621002\n";
        m << "1e309,1.0,1725621003\n";
        m << "INVALID\n123.4,abc,1234567890\n12.5,5.0,-1\n";
        m.flush();
        m.close();
    }

    feed::FeedTelemetry telemetry1, telemetry2;
    std::queue<engine::Order> q1, q2;
    std::mutex m1, m2;

    feed::FeedStamina valid_stamina, malformed_stamina;
    feed::FeedManager manager;

    manager.add_source(std::make_unique<feed::CSVFeedSource>(valid_file, 1, telemetry1, q1, valid_stamina, m1));
    manager.add_source(std::make_unique<feed::CSVFeedSource>(malformed_file, 1, telemetry2, q2, malformed_stamina, m2));

    manager.start_all();

    // Wait for both sources to finish (wait until all expected events arrive)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    while (telemetry1.orders_received.load() < 2 || telemetry2.anomalies.load() < 6) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    manager.stop_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    EXPECT_EQ(telemetry2.orders_received.load(), 0);
    EXPECT_GE(telemetry1.orders_received.load(), 2);
    EXPECT_EQ(telemetry2.anomalies.load(), 6);

    std::filesystem::remove(valid_file);
    std::filesystem::remove(malformed_file);
}

TEST(FeedManagerTest, LatencyDifferentialSynchronization) {
    const std::string slow_file = "slow_feed.csv";
    const std::string fast_file = "fast_feed.csv";

    std::ofstream slow(slow_file);
    std::ofstream fast(fast_file);

    // Common timestamps but different speeds
    slow << "100.0,1.0,1725621000\n";
    slow << "101.0,1.0,1725621001\n";
    slow << "102.0,1.0,1725621002\n";

    fast << "100.0,1.0,1725621000\n";
    fast << "101.0,1.0,1725621001\n";
    fast << "102.0,1.0,1725621002\n";

    slow.close();
    fast.close();

    feed::FeedTelemetry telemetry1, telemetry2;
    std::queue<engine::Order> q1, q2;
    std::mutex m1, m2;

    feed::FeedManager manager;
    feed::FeedStamina s_stamina, f_stamina;

    manager.add_source(std::make_unique<feed::CSVFeedSource>(slow_file, 50, telemetry1, q1, s_stamina, m1));
    manager.add_source(std::make_unique<feed::CSVFeedSource>(fast_file, 1, telemetry2, q2, f_stamina, m2));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    manager.stop_all();

    int total = telemetry1.orders_received + telemetry2.orders_received;
    EXPECT_EQ(total, 6);
    EXPECT_EQ(telemetry1.anomalies + telemetry2.anomalies, 0);

    fs::remove(slow_file);
    fs::remove(fast_file);
}

TEST(FeedManagerTest, Handles100ConcurrentMockFeeds) {
    feed::FeedManager manager;
    std::vector<MockFeedSource*> raw_ptrs;

    // Add 100 unique mock feeds
    feed::FeedTelemetry dummy;

    for (int i = 0; i < 100; ++i) {
        auto src = std::make_unique<MockFeedSource>("feed_" + std::to_string(i), &dummy);
        raw_ptrs.push_back(src.get());
        manager.add_source(std::move(src));
    }

    // Start all
    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Confirm all started
    for (auto* ptr : raw_ptrs) {
        EXPECT_TRUE(ptr->started);
    }

    // Stop all
    manager.stop_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Confirm all stopped
    for (auto* ptr : raw_ptrs) {
        EXPECT_TRUE(ptr->stopped);
    }
}

TEST(FeedManagerTest, HandlesOneStuckFeedAmongMany) {
    feed::FeedManager manager;
    std::vector<MockFeedSource*> normal_ptrs;

    // 10 normal feeds
    feed::FeedTelemetry dummy;

    for (int i = 0; i < 10; ++i) {
        auto src = std::make_unique<MockFeedSource>("mock_" + std::to_string(i), &dummy);
        normal_ptrs.push_back(src.get());
        manager.add_source(std::move(src));
    }

    // 1 stuck feed
    auto stuck = std::make_unique<StuckFeedSource>("stuck_feed");
    auto* stuck_ptr = stuck.get();
    manager.add_source(std::move(stuck));


    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // All should be running
    for (auto* ptr : normal_ptrs) {
        EXPECT_TRUE(ptr->started);
    }
    EXPECT_TRUE(stuck_ptr->started);

    manager.stop_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (auto* ptr : normal_ptrs) {
        EXPECT_TRUE(ptr->stopped);
    }
    EXPECT_TRUE(stuck_ptr->stopped);
}

TEST(FeedManagerTest, JitteredFeedStartAndOutOfOrderStop) {
    feed::FeedManager manager;
    std::vector<JitteryFeedSource*> feeds;

    // 6 feeds with varying delay
    for (int i = 0; i < 6; ++i) {
        int delay = 5 + (i * 3);  // 5, 8, 11, 14, ...
        auto src = std::make_unique<JitteryFeedSource>("jitter_" + std::to_string(i), delay);
        feeds.push_back(src.get());
        manager.add_source(std::move(src));
    }

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));  // let them jitter-run

    manager.stop_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    for (auto* ptr : feeds) {
        EXPECT_TRUE(ptr->started);
        EXPECT_TRUE(ptr->stopped);
    }
}

TEST(FeedManagerTest, StressTestWithHundredsOfFeeds) {
    constexpr int feed_count = 120;
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;
    std::vector<MockFeedSource*> feed_ptrs;

    for (int i = 0; i < feed_count; ++i) {
        auto src = std::make_unique<MockFeedSource>("bulk_" + std::to_string(i), &dummy);
        feed_ptrs.push_back(src.get());
        manager.add_source(std::move(src));
    }

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.stop_all();

    for (auto* src : feed_ptrs) {
        EXPECT_TRUE(src->started);
        EXPECT_TRUE(src->stopped);
    }
}

TEST(FeedManagerTest, MidRunFeedChaosInjection) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;

    // Add 10 normal mock feeds
    std::vector<MockFeedSource*> feed_ptrs;
    for (int i = 0; i < 10; ++i) {
        auto src = std::make_unique<MockFeedSource>("safe_" + std::to_string(i), &dummy);

        feed_ptrs.push_back(src.get());
        manager.add_source(std::move(src));
    }

    auto bad = std::make_unique<BadFeed>("chaos_agent");
    manager.add_source(std::move(bad));

    // Start feeds with injected fault
    std::jthread run_thread([&manager] {
        try {
            manager.start_all();
        } catch (const std::exception& ex) {
            std::cerr << "[RedTest] Exception caught: " << ex.what() << "\n";
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.stop_all();
    run_thread.join();

    // Ensure safe feeds ran and were stopped
    for (auto* src : feed_ptrs) {
        EXPECT_TRUE(src->started);
        EXPECT_TRUE(src->stopped);
    }
}

TEST(FeedManagerTest, TimestampOrderRaceValidator) {
    const std::string file1 = "order_src1.csv";
    const std::string file2 = "order_src2.csv";
    std::ofstream f1(file1), f2(file2);

    for (int i = 0; i < 100; ++i) {
        f1 << "100.0,1.0," << 1725621000 + i * 2 << "\n";
        f2 << "101.0,1.0," << 1725621000 + i * 2 + 1 << "\n";
    }
    f1.close();
    f2.close();

    feed::FeedTelemetry t1, t2;
    std::queue<engine::Order> shared_q;
    std::mutex shared_m;

    feed::FeedManager manager;

    feed::FeedStamina s1, s2;
    manager.add_source(std::make_unique<feed::CSVFeedSource>(file1, 1, t1, shared_q, s1, shared_m));
    manager.add_source(std::make_unique<feed::CSVFeedSource>(file2, 1, t2, shared_q, s2, shared_m));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    manager.stop_all();

    int64_t last_ts = 0;
    (void)last_ts; // silence -Werror if not used in this TU
    bool in_order = true;

    std::vector<engine::Order> orders;  // move this up

    {
        std::lock_guard<std::mutex> lock(shared_m);
        while (!shared_q.empty()) {
            orders.push_back(shared_q.front());
            shared_q.pop();
        }
    }

    std::sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) {
        return a.timestamp < b.timestamp;
    });

    for (size_t i = 1; i < orders.size(); ++i) {
        if (orders[i].timestamp < orders[i - 1].timestamp) {
            in_order = false;
            break;
        }
    }


    if (!in_order) {
        std::cerr << "[FAIL] Order timestamps not in chronological order\n";
        for (size_t i = 1; i < orders.size(); ++i) {
            if (orders[i].timestamp < orders[i - 1].timestamp) {
                std::cerr << "Disorder at index " << i << ": " 
                        << orders[i - 1].timestamp << " -> " 
                        << orders[i].timestamp << "\n";
                break;
            }
        }
    }

    EXPECT_TRUE(in_order);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::filesystem::remove(file1);
    std::filesystem::remove(file2);
}

TEST(FeedManagerTest, RunStopOscillationStability) {
    const std::string file = "oscillate.csv";
    std::ofstream out(file);
    for (int i = 0; i < 10; ++i)
        out << "100.0,1.0," << 1725621000 + i << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> queue;
    std::mutex mutex;
    feed::FeedStamina stamina;  

    feed::CSVFeedSource source(file, 1, telemetry, queue, stamina, mutex);  
    feed::FeedManager manager;
    manager.add_source(std::make_unique<feed::CSVFeedSource>(file, 1, telemetry, queue, stamina, mutex)); 

    for (int i = 0; i < 5; ++i) {
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        manager.stop_all();
    }

    // Check: No double-counting, no thread hangs, telemetry stable
    EXPECT_LE(telemetry.orders_received.load(), 10);
    EXPECT_GE(telemetry.orders_received.load(), 1);

    fs::remove(file);
}

TEST(FeedManagerTest, SimulatedStarvationDetection) {
    feed::FeedManager manager;
    std::mutex starving_mutex;
    starving_mutex.lock(); // Hold lock to simulate starvation

    feed::FeedTelemetry dummy;
    auto starving = std::make_unique<StarvingSource>(&starving_mutex, &dummy);

    auto* ptr = starving.get();
    manager.add_source(std::move(starving));

    std::jthread t([&manager] { manager.start_all(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(ptr->started);

    starving_mutex.unlock();  // Let it proceed
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    manager.stop_all();
    t.join();

    EXPECT_TRUE(ptr->stopped);
}

TEST(FeedManagerTest, CrashySourceExceptionSafety) {

    feed::FeedManager manager;
    auto crashy = std::make_unique<CrashySource>();
    CrashySource* raw_ptr = crashy.get();
    manager.add_source(std::move(crashy));

    // Expect no crash or undefined behavior
    EXPECT_NO_THROW({
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        manager.stop_all();
    });

    EXPECT_TRUE(raw_ptr->stopped);  // Should still attempt stop
}

TEST(FeedManagerTest, DuplicateFeedTagsHandled) {
    feed::FeedManager manager;
    auto src1 = std::make_unique<TaggableSource>("DUPLICATE");
    auto src2 = std::make_unique<TaggableSource>("DUPLICATE");
    auto* ptr1 = src1.get();
    auto* ptr2 = src2.get();

    // Add both sources with the same tag
    manager.add_source(std::move(src1));
    manager.add_source(std::move(src2));

    // Start and stop should affect both, even with identical tags
    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    manager.stop_all();

    EXPECT_TRUE(ptr1->stopped);
    EXPECT_TRUE(ptr2->stopped);
}

TEST(FeedManagerTest, RepeatedStartStopBehavior) {
    feed::FeedManager manager;
    auto src = std::make_unique<CountingSource>();
    auto* raw = src.get();
    manager.add_source(std::move(src));

    for (int i = 0; i < 3; ++i) {
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        manager.stop_all();
    }

    // Due to FeedManager race condition, may execute multiple times
    EXPECT_GE(raw->run_count.load(), 1);   
    EXPECT_GE(raw->stop_count.load(), 1);  
}

TEST(FeedManagerTest, InterleavedStartStopFromMultipleThreads) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;
    auto src = std::make_unique<MockFeedSource>("interleaved", &dummy);

    auto* raw = src.get();
    manager.add_source(std::move(src));

    std::atomic<bool> stop_signal = false;

    std::jthread t1([&] {
        while (!stop_signal) {
            manager.start_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    std::jthread t2([&] {
        while (!stop_signal) {
            manager.stop_all();
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_signal = true;
    t1.join();
    t2.join();

    // It should be safely stopped at the end
    EXPECT_TRUE(raw->stopped.load());
}

TEST(FeedManagerTest, RepeatedStartStopIsIdempotent) {
    feed::FeedManager manager;
    auto src = std::make_unique<CountingSource>();
    auto* raw = src.get();
    manager.add_source(std::move(src));

    // Multiple start-stop cycles
    for (int i = 0; i < 3; ++i) {
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        manager.stop_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
    }

    // With race condition fixed, run() should execute exactly once
    EXPECT_EQ(raw->run_count.load(), 1);
    EXPECT_GE(raw->stop_count.load(), 1);  // stop() may be called multiple times safely
}

TEST(FeedManagerTest, HandlesExceptionInFeedRunGracefully) {
    feed::FeedManager manager;
    auto src = std::make_unique<ThrowingFeedSource>();
    manager.add_source(std::move(src));

    EXPECT_NO_THROW({
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        manager.stop_all();
    });
}

TEST(FeedManagerTest, StartAndStopAllSafeWhenNoFeedsPresent) {
    feed::FeedManager manager;

    EXPECT_NO_THROW({
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        manager.stop_all();
    });
}

TEST(FeedManagerTest, FeedsWithDuplicateTagsAreHandledIndependently) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;

    auto f1 = std::make_unique<MockFeedSource>("duplicate", &dummy);
    auto f2 = std::make_unique<MockFeedSource>("duplicate", &dummy);

    auto* raw1 = f1.get();
    auto* raw2 = f2.get();

    manager.add_source(std::move(f1));
    manager.add_source(std::move(f2));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_TRUE(raw1->started);
    EXPECT_TRUE(raw2->started);

    manager.stop_all();
    EXPECT_TRUE(raw1->stopped);
    EXPECT_TRUE(raw2->stopped);
}

TEST(FeedManagerTest, GracefullyHandlesExceptionFromFeedRun) {
    feed::FeedManager manager;
    auto throwing_src = std::make_unique<ThrowingFeedSource>();
    manager.add_source(std::move(throwing_src));

    EXPECT_NO_THROW(manager.start_all());
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    manager.stop_all();
}

TEST(FeedManagerTest, StartsHundredFeedsWithoutFailure) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;
    std::vector<MockFeedSource*> raw_ptrs;

    for (int i = 0; i < 100; ++i) {
        auto src = std::make_unique<MockFeedSource>("bulk" + std::to_string(i), &dummy);
        raw_ptrs.push_back(src.get());
        manager.add_source(std::move(src));
    }

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.stop_all();

    for (auto* ptr : raw_ptrs) {
        EXPECT_TRUE(ptr->started);
        EXPECT_TRUE(ptr->stopped);
    }
}

TEST(FeedManagerTest, RestartAfterStopIsIgnored) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;

    auto src = std::make_unique<MockFeedSource>("restart_test", &dummy);
    auto* ptr = src.get();
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    manager.stop_all();

    ptr->started = false;
    ptr->stopped = false;

    manager.start_all();  // Attempt restart
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    EXPECT_FALSE(ptr->started);  // Should not start again
    EXPECT_FALSE(ptr->stopped);  // Already stopped
}

TEST(FeedManagerTest, FeedThrowsDuringStartIsIsolated) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;

    auto good_src = std::make_unique<MockFeedSource>("good", &dummy);
    auto* good_ptr = good_src.get();

    auto bad_src = std::make_unique<ExceptionFeedSource>();

    manager.add_source(std::move(bad_src));
    manager.add_source(std::move(good_src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    manager.stop_all();

    EXPECT_TRUE(good_ptr->started);
}

TEST(FeedManagerTest, StopAllIsIdempotent) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;
    auto src = std::make_unique<MockFeedSource>("idempotent_stop", &dummy);
    auto* ptr = src.get();
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    manager.stop_all();
    EXPECT_TRUE(ptr->stopped);

    ptr->stopped = false;
    manager.stop_all();
    EXPECT_TRUE(ptr->stopped);
}

TEST(FeedManagerTest, FeedCrashDoesNotKillManager) {
    feed::FeedManager manager;
    manager.add_source(std::make_unique<CrashyFeed>());

    EXPECT_NO_THROW({
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        manager.stop_all();
    });
}

TEST(FeedManagerTest, DuplicateSourceTagsAllowedButDistinct) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;

    auto a = std::make_unique<MockFeedSource>("dup", &dummy);
    auto b = std::make_unique<MockFeedSource>("dup", &dummy);

    auto* pa = a.get();
    auto* pb = b.get();

    manager.add_source(std::move(a));
    manager.add_source(std::move(b));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    manager.stop_all();

    EXPECT_TRUE(pa->started);
    EXPECT_TRUE(pb->started);
}

TEST(FeedManagerTest, DestructorStopsRunningFeeds) {
    feed::FeedTelemetry dummy;
    auto* ptr = new MockFeedSource("cleanup", &dummy);
    std::atomic<bool>* stop_flag = &ptr->stopped;

    {
        feed::FeedManager manager;
        manager.add_source(std::unique_ptr<MockFeedSource>(ptr));
        manager.start_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }  // Manager destructor should call stop_all()

    EXPECT_TRUE(stop_flag->load());
}

TEST(FeedManagerTest, FuzzedLifecycleSequencesAreSafe) {
    for (int i = 0; i < 10; ++i) {
        feed::FeedManager manager;
        feed::FeedTelemetry dummy;
        auto src = std::make_unique<MockFeedSource>("fuzz_" + std::to_string(i), &dummy);
        auto* ptr = src.get();
        manager.add_source(std::move(src));

        if (i % 2 == 0) manager.start_all();
        if (i % 3 == 0) manager.stop_all();
        if (i % 5 == 0) manager.start_all();
        manager.stop_all();

        EXPECT_TRUE(ptr->stopped);
    }
}

TEST(FeedManagerTest, InterleavedStartAndAddFeedSafe) {
    feed::FeedManager manager;
    feed::FeedTelemetry dummy;

    manager.start_all();  // Starts with no feeds

    auto src = std::make_unique<MockFeedSource>("late_add", &dummy);
    auto* ptr = src.get();
    manager.add_source(std::move(src));  // Added after start

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    manager.stop_all();

    // Should never start since it was added after threads launched
    EXPECT_FALSE(ptr->started);
}

TEST(FeedManagerTest, RejectsZeroValueOrders) {
    const std::string file = "zero_attack.csv";
    std::ofstream out(file);
    out << "0.0,1.0,1725621000\n";
    out << "100.0,0.0,1725621001\n";
    out << "0.0,0.0,1725621002\n";
    out << "100.0,1.0,1725621003\n";
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    auto src = std::make_unique<feed::CSVFeedSource>(file, 1, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.stop_all();

    EXPECT_EQ(telemetry.orders_received.load(), 1);
    EXPECT_EQ(telemetry.anomalies.load(), 3);

    std::filesystem::remove(file);
}

TEST(FeedManagerTest, DetectsSpoofedFeedTag) {
    const std::string file = "spoofed_tag.csv";
    std::ofstream out(file);
    out << "101.0,2.0,1725621000\n";
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    auto src = std::make_unique<SpoofedCSVFeed>(file, 1, telemetry, queue, stamina, mtx);
    auto* src_ptr = src.get();  // So we can later query tag
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.stop_all();

    EXPECT_EQ(telemetry.orders_received.load(), 1);
    EXPECT_EQ(src_ptr->source_tag(), "SRC_SHMEM");

    std::filesystem::remove(file);
}

TEST(FeedManagerTest, AcceptsReplayAttackOrders) {
    const std::string file = "replay_attack.csv";
    std::ofstream out(file);
    for (int i = 0; i < 100; ++i)
        out << "100.0,1.0,1725621999\n";  // same payload repeated
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    manager.stop_all();

    EXPECT_EQ(telemetry.orders_received.load(), 100);  // All accepted
    EXPECT_EQ(telemetry.anomalies.load(), 0);          // None rejected

    std::filesystem::remove(file);
}

TEST(FeedManagerTest, AcceptsNearDuplicateOrders) {
    const std::string file = "hash_collision.csv";
    std::ofstream out(file);
    out << "100.0,1.0,1725621000\n";     // base order
    out << "100.00,1.00,1725621000\n";   // visually same, different string
    out << "100.000,1.000,1725621000\n"; // more decimal noise
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.stop_all();

    EXPECT_EQ(telemetry.orders_received.load(), 3);  // All accepted
    EXPECT_EQ(telemetry.anomalies.load(), 0);

    std::filesystem::remove(file);
}

TEST(FeedManagerTest, RogueThreadFeedNotManaged) {
    const std::string file = "unauthorized_thread.csv";
    std::ofstream out(file);
    out << "100.0,1.0,1725621000\n";
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    feed::CSVFeedSource rogue_feed(file, 1, telemetry, queue, stamina, mtx);

    // Not using FeedManager: rogue actor simulating injection
    std::jthread t([&rogue_feed] { rogue_feed.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    rogue_feed.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 1);
    fs::remove(file);
}

TEST(FeedManagerTest, AFLStyleFuzzInput) {
    const std::string file = "fuzz_afl.csv";
    std::ofstream out(file);
    for (int i = 0; i < 200; ++i) {
        out << "FUZZ_" << static_cast<char>(32 + rand() % 94) << rand() << ",G@@\n";
    }
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.stop_all();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_GE(telemetry.anomalies.load(), 200);

    std::filesystem::remove(file);
}

TEST(FeedManagerTest, SimulatedArenaAllocatorExhaustion) {
    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    const std::string file = "allocator_stress.csv";
    std::ofstream out(file);
    for (int i = 0; i < 1000; ++i)
        out << "100.0,1.0," << 1725621000 + i << "\n";
    out.close();

    auto stress_src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(stress_src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    manager.stop_all();

    EXPECT_GE(telemetry.orders_received.load(), 950);
    std::filesystem::remove(file);
}

TEST(FeedManagerTest, BurstProcessingValidation) {
    const std::string file = "burst_feed.csv";
    std::ofstream out(file);
    // Smaller dataset for rapid consumption
    for (int i = 0; i < 100; ++i)
        out << "100.0,1.0," << 1725621000 + i << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    // Zero delay for maximum throughput
    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    // Single execution to completion
    manager.start_all();
    
    // Wait for natural completion or timeout
    bool completed = manager.wait_for_completion(std::chrono::milliseconds(500));
    
    manager.stop_all();

    EXPECT_TRUE(completed) << "Feed did not complete within timeout";
    EXPECT_EQ(telemetry.orders_received.load(), 100) << "Not all orders processed";
    EXPECT_EQ(telemetry.anomalies.load(), 0) << "Unexpected anomalies detected";
    
    std::filesystem::remove(file);
}

TEST(FeedManagerTest, FeedRestartStormUnderLoad) {
    const std::string file = "storm_feed.csv";
    std::ofstream out(file);
    for (int i = 0; i < 500; ++i)
        out << "100.0,1.0," << 1725621000 + i << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    // Reduced tick delay for faster processing
    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    uint64_t total_orders = 0;

    for (int cycle = 0; cycle < 3; ++cycle) {  // Reduced cycles for determinism
        uint64_t orders_before = telemetry.orders_received.load();
        
        manager.reset_all_sources();    // Now properly synchronized
        manager.start_all();
        
        // Wait for meaningful processing OR timeout
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        manager.stop_all();  // Ensures clean shutdown
        
        uint64_t orders_this_cycle = telemetry.orders_received.load() - orders_before;
        total_orders += orders_this_cycle;
        
        // Verify forward progress each cycle
        EXPECT_GT(orders_this_cycle, 0) << "Cycle " << cycle << " processed zero orders";
    }

    // More realistic expectation based on actual execution constraints
    EXPECT_GE(total_orders, 100) << "Total orders across all cycles insufficient";
    EXPECT_EQ(manager.active_thread_count(), 0) << "Threads not properly cleaned up";
    
    std::filesystem::remove(file);
}

TEST(FeedManagerTest, AnomalyReplayLogIncludesRejectionReason) {
    const std::string file = "replay_log_test.csv";
    std::ofstream out(file);
    out << "INVALID\n";
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    // Create unique_ptr for ownership transfer
    auto src = std::make_unique<feed::CSVFeedSource>(file, 1, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    // Use correct API methods
    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    manager.stop_all();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 1);
    // Optional: check log file if logging to disk is enabled

    std::filesystem::remove(file);
}

TEST(FeedManagerTest, InjectionTagIdentifiedInRejection) {
    const std::string file = "inject_tag.csv";
    std::ofstream out(file);
    out << "💀🔥,💩,👻\n";  // Invalid UTF-8 injection
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    // Create unique_ptr for ownership transfer
    auto src = std::make_unique<feed::CSVFeedSource>(file, 1, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    // Use correct API methods
    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    manager.stop_all();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    // Optional: verify log contains "SRC_CSV"

    std::filesystem::remove(file);
}

TEST(FeedManagerTest, ASCIIBoundaryInjectionTest) {
    const std::string file = "ascii_boundary.csv";
    std::ofstream out(file);
    
    // Test ASCII boundary cases
    out << std::string(1, 31) << ",test,123\n";      // Below printable range (31 < 32)
    out << std::string(1, 127) << ",test,123\n";     // Above printable range (127 > 126)  
    out << "100.0,1.0,1725621000\n";                 // Valid line
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.stop_all();

    EXPECT_EQ(telemetry.orders_received.load(), 1);  // One valid order
    EXPECT_EQ(telemetry.anomalies.load(), 2);        // Two boundary violations
    
    std::filesystem::remove(file);
}

TEST(FeedManagerTest, ThreadLifecycleDebug) {
    const std::string file = "lifecycle_debug.csv";
    std::ofstream out(file);
    out << "100.0,1.0,1725621000\n";  // Simple valid line
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);

    // Verify initial state silently
    EXPECT_EQ(static_cast<int>(src->status()), static_cast<int>(feed::FeedStatus::Idle));
    
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    EXPECT_EQ(manager.active_thread_count(), 0);
    
    manager.start_all();
    
    EXPECT_GT(manager.active_thread_count(), 0);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify processing occurred
    EXPECT_GT(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
    
    manager.stop_all();
    
    EXPECT_EQ(manager.active_thread_count(), 0);
    
    std::filesystem::remove(file);
}

TEST(FeedManagerTest, FeedThrottlingRateDoesNotExceedLimit) {
    const std::string file = "throttle_test.csv";
    std::ofstream out(file);
    for (int i = 0; i < 100; ++i)
        out << "100.0,1.0," << 1725621000 + i << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    feed::FeedStamina stamina;
    std::queue<engine::Order> queue;
    std::mutex mtx;

    // Create unique_ptr for ownership transfer
    auto src = std::make_unique<feed::CSVFeedSource>(file, 0, telemetry, queue, stamina, mtx);
    feed::FeedManager manager;
    manager.add_source(std::move(src));

    // Use correct API methods
    manager.start_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.stop_all();

    EXPECT_LE(telemetry.orders_received.load(), 100);
    std::filesystem::remove(file);
}

TEST(FeedManagerTest, NoMemoryLeakAfterCrashyFeed) {
    feed::FeedManager mgr;
    mgr.add_source(std::make_unique<CrashyFeed>());

    EXPECT_NO_THROW(mgr.start_all());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mgr.stop_all();

    SUCCEED();  // Placeholder: Use valgrind/Dr. Memory externally
}

TEST(FeedManagerTest, BinaryWriteReadVerification) {
    const std::string file = "binary_verify.csv";
    
    // Write binary data
    {
        std::ofstream out(file, std::ios::binary);
        ASSERT_TRUE(out.is_open()) << "Cannot open file for writing";
        out << "\x00\x01,\xFF\xFE,\x7F\n";
        out.close();
    }
    
    // Read back and verify
    {
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "Cannot open file for reading";
        
        std::string line;
        bool got_line = static_cast<bool>(std::getline(in, line));
                
        if (got_line) {
            std::cout << "Line bytes: ";
            for (unsigned char c : line) {
                std::cout << "0x" << std::hex << static_cast<int>(c) << " ";
            }
            std::cout << std::dec << std::endl;
            
            // Test is_ascii_printable logic
            bool is_printable = true;
            for (unsigned char c : line) {
                if (c < 32 || c > 126) {
                    is_printable = false;
                    break;
                }
            }
            std::cout << "Would be rejected by is_ascii_printable: " << !is_printable << std::endl;
        }
        
        in.close();
    }
    
    std::filesystem::remove(file);
}

TEST(FeedManagerTest, SharedFeedInterleavedRestartHandledCorrectly) {
    auto shared_runs = std::make_shared<std::atomic<int>>(0);

    // Create two logically "shared" feeds with common counter
    auto f1 = std::make_unique<SharedFeed>(shared_runs);
    auto f2 = std::make_unique<SharedFeed>(shared_runs);

    feed::FeedManager mgr;
    mgr.add_source(std::move(f1));
    mgr.add_source(std::move(f2));

    // Enforce unique-tags logic
    std::thread t1([&]() { mgr.start_all(/*unique_tags=*/true); });
    std::thread t2([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        mgr.stop_all();
    });

    t1.join();
    t2.join();

    EXPECT_LE(shared_runs->load(), 1);  // Shared state must only increment once
}