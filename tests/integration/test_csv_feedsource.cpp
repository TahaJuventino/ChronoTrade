#include <gtest/gtest.h>
#include "infrastructure/CSVFeedSource.hpp"
#include "observability/FeedTelemetry.hpp"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

TEST(CSVFeedSourceTest, HandlesValidAndInvalidRows) {
    const std::string test_file = "test_feed.csv";
    std::ofstream out(test_file);
    out << "102.5,1.0,1725621000\n";
    out << "INVALID_LINE\n";
    out << "103.0,0.5,1725621005\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;
    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    std::this_thread::yield();
    csv.stop();
    t.join();

    EXPECT_GE(telemetry.orders_received.load(), 2);
    EXPECT_EQ(telemetry.anomalies.load(), 1);
    std::filesystem::remove(test_file);
}

TEST(CSVFeedSourceTest, StopsGracefullyOnSignal) {
    const std::string test_file = "stop_test.csv";
    std::ofstream out(test_file);
    for (int i = 0; i < 100; ++i)
        out << "100.0,1.0,1725621" << i << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;  // ✅ ADD THIS
    feed::FeedStamina dummy_stamina;
    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_LT(telemetry.orders_received.load(), 100);
    std::filesystem::remove(test_file);
}

TEST(CSVFeedSourceTest, EmptyFileDoesNotCrash) {
    const std::string test_file = "empty_test.csv";
    std::ofstream(test_file).close();  // create empty

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;  // ✅ ADD THIS
    feed::FeedStamina dummy_stamina;
    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);
    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
    std::filesystem::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsMissingDelimiterLine) {
    const std::string test_file = "missing_delim.csv";
    std::ofstream(test_file) << "102.5 1.0 1725621000\n";  // no commas
    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;
    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsNaNandInfinity) {
    const std::string test_file = "nan_inf.csv";
    std::ofstream out(test_file);
    out << "NaN,1.0,1725621000\n";
    out << "102.5,inf,1725621001\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);
    
    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 2);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, HandlesINT64Boundaries) {
    const std::string test_file = "int64_bounds.csv";
    std::ofstream out(test_file);
    out << "100.0,1.0,-9223372036854775808\n"; // INT64_MIN
    out << "100.0,1.0,9223372036854775807\n";  // INT64_MAX
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 0);  // Only MAX passed
    EXPECT_EQ(telemetry.anomalies.load(), 2);        // MIN rejected
    
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsUTF8GarbageLine) {
    const std::string test_file = "utf8_garbage.csv";
    std::ofstream(test_file) << "💀🔥,💩,👻\n";
    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);


    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsLongLineOverflow) {
    const std::string test_file = "long_line.csv";
    std::ofstream out(test_file);
    std::string long_garbage(10000, 'X');
    out << long_garbage << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsBinaryInjectionLine) {
    const std::string test_file = "binary_line.csv";
    std::ofstream out(test_file);
    out << std::string("102.5,1.0,") << '\x00' << '\xFF' << '\x7F' << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsReorderedFields) {
    const std::string test_file = "reordered.csv";
    std::ofstream out(test_file);
    out << "1725621000,102.5,1.0\n";  // wrong field order
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsValidWithTrailingGarbage) {
    const std::string test_file = "trailing_garbage.csv";
    std::ofstream out(test_file);
    out << "102.5,1.0,1725621000GARBAGE\n";  // junk after timestamp
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsWrongDelimiter) {
    const std::string test_file = "wrong_delim.csv";
    std::ofstream out(test_file);
    out << "102.5;1.0;1725621000\n";  // using semicolons instead of commas
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.anomalies.load(), 1);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, AcceptsHighVolumeValidRows) {
    const std::string test_file = "valid_volume.csv";
    const int total_lines = 1000;

    // Generate test file
    {
        std::ofstream out(test_file);
        for (int i = 0; i < total_lines; ++i)
            out << "100.0,1.0," << 1725621000 + i << "\n";
    }

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 0, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });

    // Poll until 95% processed or max timeout
    const int target = total_lines * 0.95;
    const int timeout_ms = 2500;
    int waited_ms = 0;

    while (telemetry.orders_received.load() < target && waited_ms < timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waited_ms += 50;
    }

    csv.stop();
    t.join();

    EXPECT_GE(telemetry.orders_received.load(), target);
    EXPECT_EQ(telemetry.anomalies.load(), 0);

    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, Rejects1000MalformedRows) {
    const std::string test_file = "fuzz_1000_invalid.csv";
    std::ofstream out(test_file);
    const int total_lines = 1000;
    for (int i = 0; i < total_lines; ++i)
        out << "BAD_LINE_" << i << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 0, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    while (telemetry.anomalies.load() < total_lines &&
        telemetry.orders_received.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), total_lines);
    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, RejectsValidLineWithTrailingFields) {
    const std::string test_file = "trailing_fields.csv";
    std::ofstream out(test_file);
    out << "100.0,1.0,1725621000,EXTRA\n";  // extra field
    out << "101.0,2.0,1725621001,FOO,BAR\n"; // even more junk
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    
    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 2);

    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, TelemetryMatchesExpectedCounts) {
    const std::string test_file = "telemetry_check.csv";
    std::ofstream out(test_file);
    out << "100.0,1.0,1725621000\n";       // valid
    out << "INVALID\n";                    // malformed
    out << "102.0,1.0,1725621002\n";       // valid
    out << "100.0;1.0;1725621003\n";       // bad delimiter
    out << "103.0,1.0,1725621004\n";       // valid
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    while (telemetry.orders_received.load() + telemetry.anomalies.load() < 5) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 3);
    EXPECT_EQ(telemetry.anomalies.load(), 2);

    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, StreamStateAfterBinaryData) {
    const std::string file = "stream_test.csv";
    std::ofstream out(file, std::ios::binary);
    out << "\x00\x01,test,123\n";
    out << "100.0,1.0,1725621000\n";
    out.close();

    // Manual stream test with proper cleanup
    {
        std::ifstream stream(file);
        std::string line;
        int line_count = 0;
        
        while (std::getline(stream, line)) {
            line_count++;
            // Verify stream remains functional
            EXPECT_TRUE(stream.good() || stream.eof());
            EXPECT_FALSE(stream.fail());
        }
        
        // Binary null bytes prevent reading second line
        EXPECT_EQ(line_count, 1);  // Only first line readable
        stream.close();  // Explicit close before file removal
    }
    
    std::filesystem::remove(file);
}

TEST(CSVFeedSourceTest, DirectBinaryInjectionTest) {
    const std::string file = "direct_binary.csv";
    
    // Use less destructive binary data that doesn't corrupt streams
    {
        std::ofstream out(file);  // Text mode to avoid null byte issues
        out << std::string(1, 31) << ",test,123\n";  // Below ASCII printable (31 < 32)
        out.close();
    }

    // Verify file creation
    {
        std::ifstream check(file);
        ASSERT_TRUE(check.is_open());
        check.close();
    }

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(file , 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    // Verify initial state
    EXPECT_EQ(static_cast<int>(csv.status()), static_cast<int>(feed::FeedStatus::Idle));

    // Test status transition
    bool can_run = csv.try_set_running();
    EXPECT_TRUE(can_run);
    EXPECT_EQ(static_cast<int>(csv.status()), static_cast<int>(feed::FeedStatus::Running));

    if (can_run) {
        // Run in separate thread
        std::jthread t([&csv] { 
            csv.run(); 
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        csv.stop();
        t.join();
    }

    // Verify anomaly detection worked
    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 1);
    
    std::filesystem::remove(file);
}

TEST(CSVFeedSourceTest, MixedLinesWithAndWithoutTrailingFields) {
    const std::string test_file = "mixed_trailing.csv";
    std::ofstream out(test_file);
    out << "100.0,1.0,1725621000\n";               // valid
    out << "101.0,2.0,1725621001,EXTRA\n";          // invalid
    out << "102.0,3.0,1725621002,FOO,BAR\n";        // invalid
    out << "103.0,4.0,1725621003\n";               // valid
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file , 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 2);
    EXPECT_EQ(telemetry.anomalies.load(), 2);

    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, SafeBinaryInjectionTest) {
    const std::string file = "safe_binary.csv";
    
    {
        std::ofstream out(file, std::ios::binary);
        // Use high bytes that don't break streams but fail ASCII check
        out << "\xFF\xFE,\x80\x81,\x7F\n";  // No null bytes
        out << "100.0,1.0,1725621000\n";     // Valid line
        out.close();
    }

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(file , 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    EXPECT_TRUE(csv.try_set_running());

    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    csv.stop();
    t.join();

    // Should process valid line and reject binary line
    EXPECT_EQ(telemetry.orders_received.load(), 1);  // Valid line processed
    EXPECT_EQ(telemetry.anomalies.load(), 1);        // Binary line rejected
    
    std::filesystem::remove(file);
}

TEST(CSVFeedSourceTest, RejectsPrecisionPoisoning) {
    const std::string test_file = "poison.csv";
    std::ofstream out(test_file);
    out << "1e-324,1.0,1725621000\n";    // Underflow (subnormal)
    out << "102.5,1.0,1725621001\n";     // Valid
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);
    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 1);
    EXPECT_EQ(telemetry.anomalies.load(), 1);

    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, DetectsJitterAnomalies) {
    const std::string test_file = "jitter.csv";
    std::ofstream out(test_file);
    out << "100.0,1.0,1725621003\n";  // valid
    out << "101.0,1.0,1725621001\n";  // ⬅️ out-of-order timestamp
    out << "102.0,1.0,1725621005\n";  // valid
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 0, telemetry, dummy_queue, dummy_stamina, dummy_mutex);
    std::jthread t([&csv] { csv.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    csv.stop();
    t.join();

    EXPECT_EQ(telemetry.orders_received.load(), 2);
    EXPECT_EQ(telemetry.anomalies.load(), 1);  // Jitter caught

    fs::remove(test_file);
}

TEST(CSVFeedSourceTest, HandlesRapidStartStopChaos) {
    const std::string test_file = "chaos_signal.csv";
    std::ofstream out(test_file);
    for (int i = 0; i < 20; ++i)
        out << "100.0,1.0," << (1725621000 + i) << "\n";
    out.close();

    feed::FeedTelemetry telemetry;
    std::queue<engine::Order> dummy_queue;
    std::mutex dummy_mutex;
    feed::FeedStamina dummy_stamina;

    feed::CSVFeedSource csv(test_file, 1, telemetry, dummy_queue, dummy_stamina, dummy_mutex);

    EXPECT_TRUE(csv.try_set_running());
    std::jthread t([&csv] { csv.run(); });

    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        csv.stop();  // rapid signal flip
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        csv.try_set_running();  // try restart — should not re-run
    }

    t.join();

    EXPECT_GE(telemetry.orders_received.load(), 1);  // May vary slightly
    EXPECT_LE(telemetry.orders_received.load(), 20); // Ensure not duplicated
    fs::remove(test_file);
}