#include <gtest/gtest.h>
#include <queue>
#include <mutex>
#include <fstream>
#include "infrastructure/FeedInjector.hpp"

using namespace feed;
using namespace engine;

TEST(FeedInjectorTest, InjectsValidOrderWithOptionalFields) {
    std::ofstream f("inject_valid.json");
    f << R"({"price":101.5,"amount":2.0,"timestamp":1725000001,"tag":"OK","auth":"TRUSTED","delay_ms":0})" << "\n";
    f.close();

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;

    FeedInjector inj("inject_valid.json", telemetry, q, m);
    inj.run();

    ASSERT_EQ(telemetry.orders_received.load(), 1);
    ASSERT_EQ(q.size(), 1);
    auto o = q.front();
    EXPECT_DOUBLE_EQ(o.price, 101.5);
    EXPECT_DOUBLE_EQ(o.amount, 2.0);
    EXPECT_EQ(o.timestamp, 1725000001);
}

TEST(FeedInjectorTest, MalformedLineTriggersAnomaly) {
    std::ofstream f("inject_malformed.json");
    f << R"({"prce":100.0,"amount":2.0,"timestamp":1725})" << "\n";  // typo "prce"
    f.close();

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;

    FeedInjector inj("inject_malformed.json", telemetry, q, m);
    inj.run();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 1);
    EXPECT_TRUE(q.empty());
}

TEST(FeedInjectorTest, HandlesReplayInjectionSafely) {
    std::ofstream f("inject_replay.json");
    for (int i = 0; i < 3; ++i)
        f << R"({"price":99.0,"amount":1.0,"timestamp":1725001234,"auth":"UNVERIFIED","tag":"REPLAY_X"})" << "\n";
    f.close();

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;

    FeedInjector inj("inject_replay.json", telemetry, q, m);
    inj.run();

    EXPECT_EQ(telemetry.orders_received.load(), 3);
    EXPECT_EQ(q.size(), 3);  // No de-duplication (that's FeedHashLogger's job)
}

TEST(FeedInjectorTest, MixedChaosAndDelaysWorkCorrectly) {
    std::ofstream f("inject_chaos.json");
    f << R"({"price":120.0,"amount":1.0,"timestamp":1725000111,"delay_ms":5})" << "\n";
    f << "garbage not json\n";
    f << R"({"price":120.0,"amount":-5.0,"timestamp":1725000112})" << "\n"; // will throw (negative amount)
    f << R"({"price":121.0,"amount":1.0,"timestamp":1725000113})" << "\n";
    f.close();

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;

    FeedInjector inj("inject_chaos.json", telemetry, q, m);
    inj.run();

    EXPECT_EQ(telemetry.orders_received.load(), 2);
    EXPECT_EQ(telemetry.anomalies.load(), 2);
    EXPECT_EQ(q.size(), 2);
}

TEST(FeedInjectorTest, Handles1000ValidOrdersWithoutCrash) {
    std::ofstream f("inject_1k.json");
    for (int i = 0; i < 1000; ++i)
        f << R"({"price":1.01,"amount":1.0,"timestamp":)" << (1725000000 + i) << "}\n";

    f.close();

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;

    FeedInjector inj("inject_1k.json", telemetry, q, m);
    inj.run();

    EXPECT_EQ(telemetry.orders_received.load(), 1000);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
    EXPECT_EQ(q.size(), 1000);
}

TEST(FeedInjectorTest, MalformedDelayIsHandledGracefully) {
    std::ofstream f("inject_delay_bomb.json");
    f << R"({"price":99.0,"amount":1.0,"timestamp":1725001234,"delay_ms":"abc"})" << "\n";
    f.close();

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;

    FeedInjector inj("inject_delay_bomb.json", telemetry, q, m);
    inj.run();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 1);
}

TEST(FeedInjectorTest, DetectsTamperViaHashMismatch) {
    std::ofstream f("inject_hashbomb.json");
    f << R"({"price":100.0,"amount":2.0,"timestamp":1725000011,"tag":"ORIGINAL"})" << "\n";
    f << R"({"price":9999.9,"amount":2.0,"timestamp":1725000011,"tag":"TAMPERED"})" << "\n"; // Same ts, different price
    f.close();

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;
    FeedInjector inj("inject_hashbomb.json", telemetry, q, m);
    inj.run();

    std::ifstream log("logs/feed_hash.log");
    ASSERT_TRUE(log.is_open()) << "Log not found";
    std::string line;
    bool anomaly_found = false;
    while (std::getline(log, line)) {
        if (line.find("ANOMALY") != std::string::npos && line.find("SRC_INJECTOR") != std::string::npos) {
            anomaly_found = true;
            break;
        }
    }
    EXPECT_TRUE(anomaly_found);
}