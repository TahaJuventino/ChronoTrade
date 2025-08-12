#include <gtest/gtest.h>
#include <thread>
#include <queue>
#include <mutex>
#include "../feed/SocketFeedSource.hpp"
#include "../utils/Panic.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

using namespace feed;
using namespace engine;

namespace {
    void launch_mock_server(int port, const std::vector<std::string>& lines, int delay_ms = 0) {
        std::thread([port, lines, delay_ms]() {

    #ifdef _WIN32
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
                PANIC("WSAStartup failed (mock server)");
            }
    #endif    

            int server_fd = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = INADDR_ANY;
            addr.sin_port = htons(port);

            bind(server_fd, (sockaddr*)&addr, sizeof(addr));
            listen(server_fd, 1);

            int client_fd = accept(server_fd, nullptr, nullptr);
            for (const auto& line : lines) {
                std::string payload = line + "\n";
                send(client_fd, payload.c_str(), payload.size(), 0);
                if (delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }
            
    #ifdef _WIN32
            closesocket(client_fd);
            closesocket(server_fd);
            WSACleanup();
    #else
            close(client_fd);
            close(server_fd);
    #endif

        }).detach();
    }
}

TEST(SocketFeedSourceTest, AcceptsWellFormedJSON) {
    std::vector<std::string> data = {
        R"({"price":100.5,"amount":1.0,"timestamp":1725000001})",
        R"({"price":101.0,"amount":2.0,"timestamp":1725000002})"
    };
    launch_mock_server(7001, data);

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;
    SocketFeedSource src("127.0.0.1", 7001, telemetry, q, m);

    src.run();

    EXPECT_EQ(telemetry.orders_received.load(), 2);
    EXPECT_EQ(telemetry.anomalies.load(), 0);
    EXPECT_EQ(q.size(), 2);
}

TEST(SocketFeedSourceTest, MalformedLineTriggersAnomaly) {
    std::vector<std::string> data = {
        R"({"price":100.0,"amount":1.0,"timestamp":1725000001})",
        R"(not_a_json_line)",
        R"({"price":102.0,"amount":1.0,"timestamp":1725000002})"
    };
    launch_mock_server(7002, data);

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;
    SocketFeedSource src("127.0.0.1", 7002, telemetry, q, m);

    src.run();

    EXPECT_EQ(telemetry.orders_received.load(), 2);
    EXPECT_EQ(telemetry.anomalies.load(), 1);
    EXPECT_EQ(q.size(), 2);
}

TEST(SocketFeedSourceTest, HandlesLineFragmentationCorrectly) {
    std::vector<std::string> data = {
        R"({"price":10", "a)",
        R"(mount":2.0,"timestamp":1725000003})"
    };
    launch_mock_server(7003, data, 10);

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;
    SocketFeedSource src("127.0.0.1", 7003, telemetry, q, m);

    src.run();

    // Will be malformed because fragmentation not reassembled
    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 2);
    EXPECT_EQ(q.size(), 0);
}

TEST(SocketFeedSourceTest, AcceptsReplayOrderWithoutCrash) {
    std::vector<std::string> data = {
        R"({"price":99.99,"amount":1.0,"timestamp":1725009999})",
        R"({"price":99.99,"amount":1.0,"timestamp":1725009999})"
    };
    launch_mock_server(7004, data);

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;
    SocketFeedSource src("127.0.0.1", 7004, telemetry, q, m);

    src.run();

    EXPECT_EQ(telemetry.orders_received.load(), 2);
    EXPECT_EQ(telemetry.anomalies.load(), 0);  // No dedup yet
    EXPECT_EQ(q.size(), 2);
}

TEST(SocketFeedSourceTest, SocketClosesMidPacket) {
    std::vector<std::string> data = {
        R"({"price":102.0,"amount":1.0,"timestamp":1725)"  // truncated on purpose
    };
    launch_mock_server(7005, data);

    std::queue<Order> q;
    std::mutex m;
    FeedTelemetry telemetry;
    SocketFeedSource src("127.0.0.1", 7005, telemetry, q, m);

    src.run();

    EXPECT_EQ(telemetry.orders_received.load(), 0);
    EXPECT_EQ(telemetry.anomalies.load(), 1);
    EXPECT_TRUE(q.empty());
}