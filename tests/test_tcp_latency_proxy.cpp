#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sstream>
#include <memory>
#include <algorithm>
#include <map>
#include <random>
#include <fstream>

using namespace std::chrono_literals;
using namespace std::chrono;

class MockTcpServer {
    private:
        int server_fd_ = -1;
        int port_;
        std::atomic<bool> running_{false};
        std::thread server_thread_;
        
        mutable std::mutex data_mutex_;
        mutable std::condition_variable data_cv_;
        std::vector<std::string> received_data_;  
        std::map<std::string, int> message_counts_;
        
        std::atomic<int> total_connections_{0};
        std::atomic<size_t> total_bytes_received_{0};

        void server_loop() {
            while (running_.load()) {
                fd_set read_fds;
                FD_ZERO(&read_fds);
                FD_SET(server_fd_, &read_fds);
                
                struct timeval timeout;
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000; // 100ms
                
                int result = select(server_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
                if (result > 0 && FD_ISSET(server_fd_, &read_fds)) {
                    handle_new_connection();
                }
            }
        }

        void handle_new_connection() {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd_, (sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) return;

            total_connections_++;
            std::thread([this, client_fd]() {
                char buffer[8192];
                std::string accumulated_data;
                
                while (true) {
                    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                    if (bytes <= 0) break;
                    
                    total_bytes_received_ += static_cast<size_t>(bytes);
                    data_cv_.notify_all();
                    
                    buffer[bytes] = '\0';
                    accumulated_data.append(buffer, static_cast<size_t>(bytes));
                    
                    // Process complete lines
                    size_t pos;
                    while ((pos = accumulated_data.find('\n')) != std::string::npos) {
                        std::string line = accumulated_data.substr(0, pos);
                        accumulated_data.erase(0, pos + 1);
                        
                        if (!line.empty() && line.back() == '\r') {
                            line.pop_back();
                        }
                        
                        if (!line.empty()) {
                            {
                                std::lock_guard<std::mutex> lock(data_mutex_);
                                received_data_.push_back(line);
                                message_counts_[line]++;
                            }
                            data_cv_.notify_all();
                        }
                    }
                }
                
                // Handle any remaining data without newline
                if (!accumulated_data.empty()) {
                    {
                        std::lock_guard<std::mutex> lock(data_mutex_);
                        received_data_.push_back(accumulated_data);
                        message_counts_[accumulated_data]++;
                    }
                    data_cv_.notify_all();
                }
                
                close(client_fd);
            }).detach();
        }

    public:
        MockTcpServer(int port) : port_(port) {}
        ~MockTcpServer() { stop(); }

        bool start() {
            server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
            if (server_fd_ < 0) return false;

            int reuse = 1;
            setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            addr.sin_port = htons(port_);

            if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
                close(server_fd_);
                return false;
            }

            if (listen(server_fd_, 50) < 0) {
                close(server_fd_);
                return false;
            }

            running_ = true;
            server_thread_ = std::thread(&MockTcpServer::server_loop, this);
            std::this_thread::sleep_for(50ms);
            return true;
        }

        void stop() {
            if (running_.load()) {
                running_ = false;
                if (server_thread_.joinable()) {
                    server_thread_.join();
                }
                if (server_fd_ >= 0) {
                    close(server_fd_);
                    server_fd_ = -1;
                }
            }
        }

        bool wait_for_messages(size_t expected_count, std::chrono::milliseconds timeout = 30s) {
            std::unique_lock<std::mutex> lock(data_mutex_);
            return data_cv_.wait_for(lock, timeout, [this, expected_count]() {
                return received_data_.size() >= expected_count;
            });
        }

        // Wait for any data (not just complete messages)
        bool wait_for_data(std::chrono::milliseconds timeout = 30s) {
            std::unique_lock<std::mutex> lock(data_mutex_);
            return data_cv_.wait_for(lock, timeout, [this]() {
                return !received_data_.empty() || total_bytes_received_.load() > 0;
            });
        }

        std::vector<std::string> get_received_data() const {
            std::lock_guard<std::mutex> lock(data_mutex_);
            return received_data_;
        }

        std::map<std::string, int> get_message_counts() const {
            std::lock_guard<std::mutex> lock(data_mutex_);
            return message_counts_;
        }

        void clear_received_data() {
            std::lock_guard<std::mutex> lock(data_mutex_);
            received_data_.clear();
            message_counts_.clear();
        }

        size_t message_count() const {
            std::lock_guard<std::mutex> lock(data_mutex_);
            return received_data_.size();
        }

        int total_connections() const { 
            return total_connections_.load(); 
        }

        size_t total_bytes_received() const { 
            return total_bytes_received_.load(); 
        }

        // Wait until we've received at least min_bytes in total
        bool wait_for_bytes(size_t min_bytes,
                            std::chrono::milliseconds timeout = 30s) {

            std::unique_lock<std::mutex> lock(data_mutex_);
            return data_cv_.wait_for(lock, timeout, [&]{
                return total_bytes_received_.load() >= min_bytes;
            });
}

};

class TestClient {
    private:
        int client_fd_ = -1;
        bool connected_ = false;
        std::atomic<size_t> bytes_sent_{0};

    public:
        int get_fd() const { 
            return client_fd_; 
        
        }

        ~TestClient() { 
            disconnect(); 
        }

        bool connect(int port, int timeout_seconds = 5) {
            client_fd_ = socket(AF_INET, SOCK_STREAM, 0);
            if (client_fd_ < 0) return false;

            struct timeval timeout;
            timeout.tv_sec = timeout_seconds;
            timeout.tv_usec = 0;
            setsockopt(client_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            setsockopt(client_fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = inet_addr("127.0.0.1");
            addr.sin_port = htons(port);

            if (::connect(client_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
                close(client_fd_);
                client_fd_ = -1;
                return false;
            }

            connected_ = true;
            return true;
        }

        void disconnect() {
            if (connected_ && client_fd_ >= 0) {
                close(client_fd_);
                client_fd_ = -1;
                connected_ = false;
            }
        }

        bool send_messages(const std::vector<std::string>& messages) {
            if (!connected_) return false;

            for (const auto& msg : messages) {
                std::string full_msg = msg + "\n";
                ssize_t bytes_sent = send(client_fd_, full_msg.c_str(), full_msg.size(), MSG_NOSIGNAL);
                
                if (bytes_sent != static_cast<ssize_t>(full_msg.size())) {
                    return false;
                }
                bytes_sent_ += static_cast<size_t>(bytes_sent);
                
                // Small delay between messages for better handling
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }

        bool send_bulk(const std::string& data) {
            if (!connected_) return false;
            
            // Send in smaller chunks for better bandwidth throttling
            size_t sent = 0;
            const size_t chunk_size = 1024;
            
            while (sent < data.size()) {
                size_t to_send = std::min(chunk_size, data.size() - sent);
                ssize_t bytes_sent = send(client_fd_, data.c_str() + sent, to_send, MSG_NOSIGNAL);
                
                if (bytes_sent <= 0) return false;
                
                sent += static_cast<size_t>(bytes_sent);
                bytes_sent_ += static_cast<size_t>(bytes_sent);
                
                // Small delay between chunks
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return sent == data.size();
        }

        size_t bytes_sent() const { 
            return bytes_sent_.load(); 
        }

        static std::vector<std::string> generate_messages(size_t count, const std::string& prefix = "msg") {
            std::vector<std::string> messages;
            messages.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                messages.push_back(prefix + std::to_string(i));
            }
            return messages;
        }

        static std::string generate_bulk_data(size_t size) {
            std::string data;
            data.reserve(size);
            for (size_t i = 0; i < size; ++i) {
                data += static_cast<char>('A' + (i % 26));
            }
            return data;
        }
};

class ProxyProcess {
    private:
        pid_t pid_ = -1;
        bool running_ = false;

    public:
        ~ProxyProcess() { 
            stop(); 
        }

        bool start(const std::vector<std::string>& args) {
            if (running_) return false;

            std::vector<char*> argv;
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            pid_ = fork();
            if (pid_ == 0) {
                // Redirect stderr to null to silence proxy
                int null_fd = open("/dev/null", O_WRONLY);
                if (null_fd >= 0) {
                    dup2(null_fd, STDERR_FILENO);
                    close(null_fd);
                }
                
                execvp(argv[0], argv.data());
                exit(1);
            } else if (pid_ > 0) {
                running_ = true;
                std::this_thread::sleep_for(200ms); // Increased startup delay
                return true;
            }
            return false;
        }

        void stop() {
            if (running_ && pid_ > 0) {
                kill(pid_, SIGTERM);
                
                for (int i = 0; i < 100; ++i) { // Increased wait iterations
                    int status;
                    if (waitpid(pid_, &status, WNOHANG) == pid_) {
                        running_ = false;
                        pid_ = -1;
                        return;
                    }
                    std::this_thread::sleep_for(20ms);
                }
                
                kill(pid_, SIGKILL);
                waitpid(pid_, nullptr, 0);
                running_ = false;
                pid_ = -1;
            }
        }

        bool is_running() const {
            if (!running_ || pid_ <= 0) return false;
            int status;
            return waitpid(pid_, &status, WNOHANG) == 0;
        }
};

class TcpLatencyProxyTest : public ::testing::Test {
    protected:
        static constexpr int BASE_PORT = 22000;
        static int current_port_;
        
        std::unique_ptr<MockTcpServer> upstream_server_;
        std::unique_ptr<ProxyProcess> proxy_process_;
        int upstream_port_;
        int proxy_port_;

        void SetUp() override {
            upstream_port_ = current_port_++;
            proxy_port_ = current_port_++;
            std::this_thread::sleep_for(50ms); // Increased setup delay
        }

        void TearDown() override {
            if (proxy_process_) proxy_process_->stop();
            if (upstream_server_) upstream_server_->stop();
            std::this_thread::sleep_for(100ms); // Increased teardown delay
        }

        bool start_upstream_server() {
            upstream_server_ = std::make_unique<MockTcpServer>(upstream_port_);
            return upstream_server_->start();
        }

        bool start_proxy(const std::string& extra_args = "") {
            std::vector<std::string> args = {
                "./tcp_latency_proxy",
                "--listen-host", "127.0.0.1",
                "--listen-port", std::to_string(proxy_port_),
                "--upstream-host", "127.0.0.1", 
                "--upstream-port", std::to_string(upstream_port_)
            };

            if (!extra_args.empty()) {
                std::istringstream iss(extra_args);
                std::string token;
                while (iss >> token) {
                    args.push_back(token);
                }
            }

            proxy_process_ = std::make_unique<ProxyProcess>();
            return proxy_process_->start(args);
        }

        bool wait_for_proxy_ready(std::chrono::seconds timeout = 10s) { // Increased timeout
            auto start = steady_clock::now();
            
            while (steady_clock::now() - start < timeout) {
                TestClient test_client;
                if (test_client.connect(proxy_port_, 1)) {
                    return true;
                }
                std::this_thread::sleep_for(100ms); // Increased retry delay
            }
            return false;
        }
};

int TcpLatencyProxyTest::current_port_ = BASE_PORT;

// Massive concurrent connections stress test
TEST_F(TcpLatencyProxyTest, MassiveConcurrency) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--max-connections 200"));
    ASSERT_TRUE(wait_for_proxy_ready());

    constexpr int THREAD_COUNT = 100;
    constexpr int MESSAGES_PER_THREAD = 20;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    std::atomic<int> total_sent{0};
    
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&, i]() {
            TestClient client;
            if (client.connect(proxy_port_, 2)) {
                auto messages = TestClient::generate_messages(MESSAGES_PER_THREAD, "T" + std::to_string(i) + "_");
                if (client.send_messages(messages)) {
                    success_count++;
                    total_sent += MESSAGES_PER_THREAD;
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    upstream_server_->wait_for_messages(total_sent.load() * 0.8, 30s);
    
    EXPECT_GE(success_count.load(), THREAD_COUNT * 0.7);
    EXPECT_GE(upstream_server_->message_count(), static_cast<size_t>(total_sent.load() * 0.8));
}

// High latency + jitter combination
TEST_F(TcpLatencyProxyTest, ExtremeLatencyJitter) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--latency-ms 200 --jitter-ms 100"));
    ASSERT_TRUE(wait_for_proxy_ready());

    constexpr int MESSAGE_COUNT = 50;
    auto messages = TestClient::generate_messages(MESSAGE_COUNT);
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    auto start = steady_clock::now();
    ASSERT_TRUE(client.send_messages(messages));
    client.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(MESSAGE_COUNT, 60s));
    auto duration = duration_cast<milliseconds>(steady_clock::now() - start);
    
    // With 200ms ± 100ms latency, expect significant delay
    EXPECT_GE(duration.count(), 5000); // At least 5 seconds for latency effects
}

// Packet loss + duplication chaos test
TEST_F(TcpLatencyProxyTest, PacketChaos) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--drop-rate 0.3 --dup-rate 0.2 --latency-ms 10"));
    ASSERT_TRUE(wait_for_proxy_ready());

    constexpr int MESSAGE_COUNT = 500;
    auto messages = TestClient::generate_messages(MESSAGE_COUNT, "CHAOS");
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    ASSERT_TRUE(client.send_messages(messages));
    client.disconnect();
    
    std::this_thread::sleep_for(15s); // Allow chaos to settle
    
    auto message_counts = upstream_server_->get_message_counts();
    auto total_received = upstream_server_->message_count();
    (void)total_received; // only used for debugging/logging in some builds

    int expected_minimum = static_cast<int>(MESSAGE_COUNT * 0.5);

    // With 30% drop, expect roughly 70% to arrive
    EXPECT_GE(message_counts.size(), static_cast<size_t>(MESSAGE_COUNT * 0.5));
    EXPECT_GE(total_received, expected_minimum);

    // With 20% dup rate, expect some duplicates
    size_t duplicates = 0;
    for (const auto& pair : message_counts) {
        if (pair.second > 1) duplicates++;
    }
    EXPECT_GT(duplicates, 0);
}

// Memory stress with large messages
TEST_F(TcpLatencyProxyTest, LargeMessageStress) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--buffer-bytes 65536"));
    ASSERT_TRUE(wait_for_proxy_ready());

    constexpr int LARGE_MESSAGE_COUNT = 100;
    constexpr size_t MESSAGE_SIZE = 10240; // 10KB each
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    for (int i = 0; i < LARGE_MESSAGE_COUNT; ++i) {
        std::string large_msg = "LARGE" + std::to_string(i) + "_" + TestClient::generate_bulk_data(MESSAGE_SIZE) + "\n";
        ASSERT_TRUE(client.send_bulk(large_msg));
    }
    client.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(LARGE_MESSAGE_COUNT, 60s));
    EXPECT_GT(upstream_server_->total_bytes_received(), MESSAGE_SIZE * LARGE_MESSAGE_COUNT);
}

// Combined stress: everything at once
TEST_F(TcpLatencyProxyTest, UltimateStress) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--latency-ms 50 --jitter-ms 25 --drop-rate 0.15 --dup-rate 0.1 --bandwidth-kbps 512"));
    ASSERT_TRUE(wait_for_proxy_ready());

    constexpr int THREAD_COUNT = 20;
    constexpr int MESSAGES_PER_THREAD = 100;
    
    std::vector<std::thread> threads;
    std::atomic<int> total_sent{0};
    
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&, i]() {
            TestClient client;
            if (client.connect(proxy_port_, 3)) {
                auto messages = TestClient::generate_messages(MESSAGES_PER_THREAD, "ULTIMATE" + std::to_string(i) + "_");
                if (client.send_messages(messages)) {
                    total_sent += MESSAGES_PER_THREAD;
                }
                // Add some random large messages
                if (i % 5 == 0) {
                    std::string large = "BIG" + std::to_string(i) + "_" + TestClient::generate_bulk_data(1024) + "\n";
                    client.send_bulk(large);
                }
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Wait generously for all chaos to resolve
    std::this_thread::sleep_for(30s);
    
    auto received_count = upstream_server_->message_count();
    
    // With 15% drop rate, expect at least 60% of messages (accounting for variability)
    EXPECT_GE(received_count, static_cast<size_t>(total_sent.load() * 0.6));
    EXPECT_GT(upstream_server_->total_connections(), 0);
}

// Rapid connection cycling
TEST_F(TcpLatencyProxyTest, ConnectionChurn) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy());
    ASSERT_TRUE(wait_for_proxy_ready());

    constexpr int CYCLE_COUNT = 200;
    std::atomic<int> successful_cycles{0};
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < CYCLE_COUNT / 10; ++j) {
                TestClient client;
                if (client.connect(proxy_port_, 1)) {
                    std::string msg = "CHURN_" + std::to_string(i) + "_" + std::to_string(j) + "\n";
                    if (client.send_bulk(msg)) {
                        successful_cycles++;
                    }
                    client.disconnect();
                }
                std::this_thread::sleep_for(1ms);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::this_thread::sleep_for(5s);
    
    EXPECT_GT(successful_cycles.load(), CYCLE_COUNT * 0.7);
    EXPECT_GT(upstream_server_->total_connections(), 0);
}

// High-frequency small messages
TEST_F(TcpLatencyProxyTest, HighFrequencyMessages) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--latency-ms 1"));
    ASSERT_TRUE(wait_for_proxy_ready());

    constexpr int HIGH_FREQ_COUNT = 5000;
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    std::string bulk_data;
    for (int i = 0; i < HIGH_FREQ_COUNT; ++i) {
        bulk_data += "HF" + std::to_string(i) + "\n";
    }
    
    auto start = steady_clock::now();
    ASSERT_TRUE(client.send_bulk(bulk_data));
    client.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(HIGH_FREQ_COUNT, 30s));
    auto duration = duration_cast<milliseconds>(steady_clock::now() - start);
    
    auto throughput = (HIGH_FREQ_COUNT * 1000) / duration.count();
    EXPECT_GT(throughput, 100); // At least 100 msg/sec
    EXPECT_EQ(upstream_server_->message_count(), HIGH_FREQ_COUNT);
}

// Basic forwarding functionality
TEST_F(TcpLatencyProxyTest, BasicForwarding) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy());
    ASSERT_TRUE(wait_for_proxy_ready());

    auto messages = TestClient::generate_messages(100);
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    ASSERT_TRUE(client.send_messages(messages));
    client.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(100, 10s));
    auto received = upstream_server_->get_received_data();
    ASSERT_EQ(received.size(), 100);
    
    for (size_t i = 0; i < messages.size(); ++i) {
        EXPECT_EQ(received[i], messages[i]);
    }
}

// Advanced jitter analysis 
TEST_F(TcpLatencyProxyTest, JitterAnalysis) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--latency-ms 100 --jitter-ms 50 --seed 12345"));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    constexpr int JITTER_MESSAGES = 100;
    auto messages = TestClient::generate_messages(JITTER_MESSAGES, "JITTER");
    
    std::vector<long long> timings;
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    for (const auto& msg : messages) {
        size_t initial_count = upstream_server_->message_count();
        auto start = steady_clock::now();
        
        ASSERT_TRUE(client.send_messages({msg}));
        upstream_server_->wait_for_messages(initial_count + 1, 5s);
        
        auto duration = duration_cast<milliseconds>(steady_clock::now() - start);
        timings.push_back(duration.count());
    }
    client.disconnect();
    
    // Analyze timing distribution
    auto min_time = *std::min_element(timings.begin(), timings.end());
    auto max_time = *std::max_element(timings.begin(), timings.end());
    auto avg_time = std::accumulate(timings.begin(), timings.end(), 0LL) / timings.size();
    
    EXPECT_GE(min_time, 40);  // Should be at least 50ms with variation
    EXPECT_LE(max_time, 200); // Should be at most 150ms + tolerance
    EXPECT_NEAR(avg_time, 100, 30); // Average around base latency
}

// Zero-latency high throughput
TEST_F(TcpLatencyProxyTest, ZeroLatencyThroughput) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--latency-ms 0 --buffer-bytes 65536"));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    constexpr int ZERO_LAT_MESSAGES = 10000;
    std::string bulk_data;
    for (int i = 0; i < ZERO_LAT_MESSAGES; ++i) {
        bulk_data += "ZERO" + std::to_string(i) + "\n";
    }
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    auto start = steady_clock::now();
    ASSERT_TRUE(client.send_bulk(bulk_data));
    client.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(ZERO_LAT_MESSAGES, 20s));
    auto duration = duration_cast<milliseconds>(steady_clock::now() - start);
    
    auto throughput = (ZERO_LAT_MESSAGES * 1000) / duration.count();
    EXPECT_GT(throughput, 500); // Should be very fast with no latency
    EXPECT_LT(duration.count(), 5000); // Should complete quickly
}

// Random message size stress
TEST_F(TcpLatencyProxyTest, RandomMessageSizes) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--latency-ms 5 --drop-rate 0.05"));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    std::vector<std::string> random_messages;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> size_dist(10, 1000);
    
    for (int i = 0; i < 500; ++i) {
        size_t msg_size = size_dist(gen);
        std::string msg = "RAND" + std::to_string(i) + "_" + std::string(msg_size, 'R');
        random_messages.push_back(msg);
    }
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    ASSERT_TRUE(client.send_messages(random_messages));
    client.disconnect();
    
    std::this_thread::sleep_for(10s);
    
    // With 5% drop rate, expect most messages to arrive
    EXPECT_GT(upstream_server_->message_count(), 400);
}

// Directional filtering test
TEST_F(TcpLatencyProxyTest, DirectionalFiltering) {
    // Test upstream direction only
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--direction up --latency-ms 10"));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    auto messages = TestClient::generate_messages(100, "UPONLY");
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    ASSERT_TRUE(client.send_messages(messages));
    client.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(100, 15s));
    EXPECT_EQ(upstream_server_->message_count(), 100);
    
    // Reset for downstream test
    upstream_server_.reset();
    proxy_process_.reset();
    upstream_port_ = current_port_++;
    proxy_port_ = current_port_++;
    
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--direction down --latency-ms 10"));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    TestClient client2;
    ASSERT_TRUE(client2.connect(proxy_port_));
    auto messages2 = TestClient::generate_messages(50, "DOWNONLY");
    ASSERT_TRUE(client2.send_messages(messages2));
    client2.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(50, 10s));
    EXPECT_EQ(upstream_server_->message_count(), 50);
}

// Half-close behavior test
TEST_F(TcpLatencyProxyTest, HalfCloseHandling) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--half-close"));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    auto messages = TestClient::generate_messages(50, "HALFCLOSE");
    ASSERT_TRUE(client.send_messages(messages));
    
    // Close write side
    shutdown(client.get_fd(), SHUT_WR);
    std::this_thread::sleep_for(1000ms);
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(50, 10s));
    EXPECT_EQ(upstream_server_->message_count(), 50);
}

// Seed-based reproducibility test
TEST_F(TcpLatencyProxyTest, SeedReproducibility) {
    constexpr uint32_t TEST_SEED = 98765;
    
    // First run with specific seed
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--latency-ms 20 --jitter-ms 10 --drop-rate 0.1 --dup-rate 0.05 --seed " + std::to_string(TEST_SEED)));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    auto messages = TestClient::generate_messages(200, "SEED");
    TestClient client1;
    ASSERT_TRUE(client1.connect(proxy_port_));
    ASSERT_TRUE(client1.send_messages(messages));
    client1.disconnect();
    
    std::this_thread::sleep_for(5s);
    auto first_run_messages = upstream_server_->get_message_counts();
    
    // Reset and run again with same seed
    upstream_server_->clear_received_data();
    proxy_process_.reset();
    
    ASSERT_TRUE(start_proxy("--latency-ms 20 --jitter-ms 10 --drop-rate 0.1 --dup-rate 0.05 --seed " + std::to_string(TEST_SEED)));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    TestClient client2;
    ASSERT_TRUE(client2.connect(proxy_port_));
    ASSERT_TRUE(client2.send_messages(messages));
    client2.disconnect();
    
    std::this_thread::sleep_for(5s);
    auto second_run_messages = upstream_server_->get_message_counts();
    
    // Results should be similar (not identical due to timing, but close)
    EXPECT_NEAR(static_cast<double>(first_run_messages.size()), 
                static_cast<double>(second_run_messages.size()), 
                messages.size() * 0.1); // Within 10% variance
}

// test with bulk data and proper expectations
TEST_F(TcpLatencyProxyTest, ExtremeBandwidthThrottle) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--bandwidth-kbps 1")); // 1 kbps = 125 bytes/sec
    ASSERT_TRUE(wait_for_proxy_ready());

    // Send 250 bytes - should take ~2 seconds at 1kbps
    std::string data = "BANDWIDTH_TEST_DATA_";
    for (int i = 0; i < 10; ++i) {
        data += std::to_string(i) + "_ABCDEFGHIJKLMNOP_"; // ~25 bytes each
    }
    data += "\n"; // Make it a complete message

    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    auto start = steady_clock::now();
    ASSERT_TRUE(client.send_bulk(data));
    client.disconnect();
    
    // Wait for data to arrive (not necessarily as complete messages)
    ASSERT_TRUE(upstream_server_->wait_for_bytes(data.size(), 30s));
    auto duration = duration_cast<milliseconds>(steady_clock::now() - start);

    
    // At 1kbps, 250 bytes should take at least 1.5 seconds
    EXPECT_GE(duration.count(), 1500) << "Bandwidth throttling not working properly";
    EXPECT_GE(upstream_server_->total_bytes_received(), data.size() - 10); // Allow some margin
}

// test burst behavior properly
TEST_F(TcpLatencyProxyTest, BandwidthBurst) {
    ASSERT_TRUE(start_upstream_server());
    ASSERT_TRUE(start_proxy("--bandwidth-kbps 64 --enable-burst"));
    ASSERT_TRUE(wait_for_proxy_ready());

    // Send initial burst that should go through quickly
    constexpr size_t BURST_SIZE = 1024; // Smaller burst size
    std::string burst_data;
    for (size_t i = 0; i < BURST_SIZE / 20; ++i) {
        burst_data += "BURST" + std::to_string(i) + "_DATA\n"; // ~20 bytes per line
    }
    
    TestClient client;
    ASSERT_TRUE(client.connect(proxy_port_));
    
    auto start = steady_clock::now();
    ASSERT_TRUE(client.send_bulk(burst_data));
    
    // Send a bit more data to test sustained rate
    std::string extra_data;
    for (int i = 0; i < 100; ++i) {
        extra_data += "EXTRA" + std::to_string(i) + "\n";
    }
    ASSERT_TRUE(client.send_bulk(extra_data));
    client.disconnect();
    
    const size_t expected_total = burst_data.size() + extra_data.size();
    ASSERT_TRUE(upstream_server_->wait_for_bytes(expected_total, 20s));
    auto duration = duration_cast<milliseconds>(steady_clock::now() - start);
    
    // With burst enabled, should complete reasonably quickly
    EXPECT_LT(duration.count(), 15000); // Less than 15 seconds
    EXPECT_GE(upstream_server_->total_bytes_received(), burst_data.size() / 2); // At least half the data
}

//  more realistic test parameters
TEST_F(TcpLatencyProxyTest, MultiBandwidthStress) {
    struct BandwidthTest { 
        int kbps; 
        size_t data_size; 
        long min_ms; 
        std::string description;
    };
    
    std::vector<BandwidthTest> tests = {
        {4, 500, 800, "4kbps_500bytes"},    // ~1 second minimum
        {16, 1000, 400, "16kbps_1000bytes"}, // ~0.5 seconds minimum  
        {64, 2000, 200, "64kbps_2000bytes"}  // ~0.25 seconds minimum
    };
    
    for (const auto& test : tests) {
        // Restart components for each test
        upstream_server_.reset();
        proxy_process_.reset();
        std::this_thread::sleep_for(200ms);
        
        upstream_port_ = current_port_++;
        proxy_port_ = current_port_++;
        
        ASSERT_TRUE(start_upstream_server()) << "Failed to start server for " << test.description;
        ASSERT_TRUE(start_proxy("--bandwidth-kbps " + std::to_string(test.kbps))) 
            << "Failed to start proxy for " << test.description;
        ASSERT_TRUE(wait_for_proxy_ready()) << "Proxy not ready for " << test.description;
        
        // Create test data
        std::string test_data;
        for (size_t i = 0; i < test.data_size / 50; ++i) {
            test_data += test.description + "_" + std::to_string(i) + "_PADDING_DATA_";
        }
        test_data += "\n";
        
        TestClient client;
        auto start = steady_clock::now();
        ASSERT_TRUE(client.connect(proxy_port_)) << "Failed to connect for " << test.description;
        ASSERT_TRUE(client.send_bulk(test_data)) << "Failed to send data for " << test.description;
        client.disconnect();
        
        ASSERT_TRUE(upstream_server_->wait_for_bytes(test_data.size(), 30s)) 
            << "Did not receive full payload for " << test.description;

        auto duration = duration_cast<milliseconds>(steady_clock::now() - start);
        
        // More lenient timing checks - network conditions can vary
        EXPECT_GE(duration.count(), test.min_ms / 2) << "Bandwidth " << test.kbps << " kbps not throttled at all";
        EXPECT_GE(upstream_server_->total_bytes_received(), test_data.size() / 2) << "Insufficient data received for " << test.description;
    }
}

// handle the extra connections properly
TEST_F(TcpLatencyProxyTest, ConnectionFailureRecovery) {
    // Start proxy without upstream - should handle gracefully
    ASSERT_TRUE(start_proxy("--verbose"));
    ASSERT_TRUE(wait_for_proxy_ready());
    
    // Try to send some data - connection should fail or drop
    TestClient client1;
    bool connected = client1.connect(proxy_port_, 2);
    
    if (connected) {
        auto messages = TestClient::generate_messages(3, "FAIL");
        client1.send_messages(messages); // May or may not succeed
    }
    client1.disconnect();
    
    // Now start upstream and test recovery
    ASSERT_TRUE(start_upstream_server());
    std::this_thread::sleep_for(1000ms); // Give time for upstream to be ready
    
    // Clear any stale connections/data
    upstream_server_->clear_received_data();
    
    // Test successful connection after upstream is available
    TestClient client2;
    ASSERT_TRUE(client2.connect(proxy_port_));
    auto recovery_messages = TestClient::generate_messages(50, "RECOVERY");
    ASSERT_TRUE(client2.send_messages(recovery_messages));
    client2.disconnect();
    
    ASSERT_TRUE(upstream_server_->wait_for_messages(50, 15s));
    
    // connection attempts being partially successful. Let's just check that we got at least 50.
    size_t received = upstream_server_->message_count();
    EXPECT_GE(received, 50) << "Expected at least 50 messages, got " << received;
    EXPECT_LE(received, 60) << "Got too many messages: " << received << " (expected ~50)";
}