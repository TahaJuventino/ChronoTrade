#include <gtest/gtest.h>
#include <fstream>
#include <thread>
#include <chrono>
#include "security/FeedHashLogger.hpp"

using namespace security;

TEST(FeedHashLoggerTest, IdenticalInputGivesSameHash) {
    std::string input = "123.45,1.0,1725000001";
    auto h1 = FeedHashLogger::compute_sha256(input);
    auto h2 = FeedHashLogger::compute_sha256(input);
    EXPECT_EQ(h1, h2);
}

TEST(FeedHashLoggerTest, DifferentInputGivesDifferentHash) {
    std::string input1 = "123.45,1.0,1725000001";
    std::string input2 = "123.46,1.0,1725000001";
    auto h1 = FeedHashLogger::compute_sha256(input1);
    auto h2 = FeedHashLogger::compute_sha256(input2);
    EXPECT_NE(h1, h2);
}

TEST(FeedHashLoggerTest, LogsCleanPacket) {
    std::string packet = "120.0,1.0,1725000100";
    std::string hash = FeedHashLogger::compute_sha256(packet);
    FeedHashLogger::log_packet(packet, hash, "SRC_TEST");

    // Delay to ensure flush + write complete
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::ifstream log("logs/feed_hash.log");
    ASSERT_TRUE(log.is_open()) << "Log file could not be opened";

    std::string line;
    bool found = false;
    while (std::getline(log, line)) {
        if (line.find("SRC_TEST") != std::string::npos && line.find(hash) != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected clean packet log entry not found";
}

TEST(FeedHashLoggerTest, LogsAnomalyWhenHashesDiffer) {
    std::string original = "120.0,1.0,1725000100";
    std::string tampered = "120.1,1.0,1725000100";
    std::string h1 = FeedHashLogger::compute_sha256(original);
    std::string h2 = FeedHashLogger::compute_sha256(tampered);
    ASSERT_NE(h1, h2);

    FeedHashLogger::log_anomaly(h1, h2, "SRC_ANOMALY");

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    std::ifstream log("logs/feed_hash.log");
    ASSERT_TRUE(log.is_open()) << "Log file could not be opened";

    std::string line;
    bool found = false;
    while (std::getline(log, line)) {
        if (line.find("SRC_ANOMALY") != std::string::npos &&
            line.find("ANOMALY") != std::string::npos &&
            line.find(h1) != std::string::npos &&
            line.find(h2) != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected anomaly log entry not found";
}

TEST(FeedHashLoggerTest, HandlesEmptyPacketGracefully) {
    std::string packet = "";
    std::string hash = FeedHashLogger::compute_sha256(packet);
    EXPECT_FALSE(hash.empty());

    FeedHashLogger::log_packet(packet, hash, "SRC_EMPTY");
    std::ifstream log("logs/feed_hash.log");
    ASSERT_TRUE(log.is_open()) << "Log file could not be opened";

    std::string line;
    bool found = false;
    while (std::getline(log, line)) {
        if (line.find("SRC_EMPTY") != std::string::npos && line.find(hash) != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(FeedHashLoggerTest, HandlesUnicodeOrBinaryInput) {
    std::string packet = "\xFF\x00hello\xE2\x82\xAC";
    std::string hash = FeedHashLogger::compute_sha256(packet);
    EXPECT_FALSE(hash.empty());
}

TEST(FeedHashLoggerTest, LogsMultiplePacketsAppendsCorrectly) {
    std::ofstream clear("logs/feed_hash.log", std::ios::trunc);
    clear.close();

    std::vector<std::string> packets = {
        "100.0,2.0,1725000001",
        "101.0,3.0,1725000002",
        "102.0,4.0,1725000003"
    };

    std::vector<std::string> tags;
    for (size_t i = 0; i < packets.size(); ++i) {
        std::string tag = "SRC_MULTI_" + std::to_string(i);
        std::string hash = FeedHashLogger::compute_sha256(packets[i]);
        FeedHashLogger::log_packet(packets[i], hash, tag);
        tags.push_back(tag);
    }

    std::ifstream log("logs/feed_hash.log");
    ASSERT_TRUE(log.is_open()) << "Log file could not be opened";

    std::set<std::string> matched;
    std::string line;
    while (std::getline(log, line)) {
        for (const auto& tag : tags) {
            if (line.find(tag) != std::string::npos)
                matched.insert(tag);
        }
    }
    EXPECT_EQ(matched.size(), 3);
}

TEST(FeedHashLoggerTest, LogAnomalyIncludesBothHashes) {
    std::string original = "100.0,1.0,1725000001";
    std::string tampered = "100.1,1.0,1725000001";
    std::string h1 = FeedHashLogger::compute_sha256(original);
    std::string h2 = FeedHashLogger::compute_sha256(tampered);
    FeedHashLogger::log_anomaly(h1, h2, "SRC_DIFF_HASH");

    std::ifstream log("logs/feed_hash.log");
    ASSERT_TRUE(log.is_open()) << "Log file could not be opened";

    std::string line;
    bool found = false;
    while (std::getline(log, line)) {
        if (line.find("SRC_DIFF_HASH") != std::string::npos &&
            line.find("ANOMALY") != std::string::npos &&
            line.find(h1) != std::string::npos &&
            line.find(h2) != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(FeedHashLoggerTest, HashCollisionIsStatisticallyUnlikely) {
    std::string s1 = "abc";
    std::string s2 = "def";
    std::string h1 = FeedHashLogger::compute_sha256(s1);
    std::string h2 = FeedHashLogger::compute_sha256(s2);
    EXPECT_NE(h1, h2);
}

TEST(FeedHashLoggerTest, HandlesVeryLargeInput) {
    std::string packet(10'000, 'A'); // 10K 'A's
    std::string hash = FeedHashLogger::compute_sha256(packet);
    EXPECT_FALSE(hash.empty());

    FeedHashLogger::log_packet(packet, hash, "SRC_LARGE");
    std::ifstream log("logs/feed_hash.log");
    ASSERT_TRUE(log.is_open()) << "Log file could not be opened";

    std::string line;
    bool found = false;
    while (std::getline(log, line)) {
        if (line.find("SRC_LARGE") != std::string::npos && line.find(hash) != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}