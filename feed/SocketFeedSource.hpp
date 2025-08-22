#pragma once

#include "feed/IFeedSource.hpp"
#include "feed/FeedTelemetry.hpp"
#include "core/Order.hpp"

#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>
#include <queue>
#include <mutex>
#include <cstring>

#ifdef __linux__
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
#else
  #error "SocketFeedSource only supported on Linux"
#endif

namespace feed {

    class SocketFeedSource : public IFeedSource {
        public:
            SocketFeedSource(const std::string& host, 
                            int port,
                            FeedTelemetry& telemetry,
                            std::queue<engine::Order>& out_queue,
                            std::mutex& queue_mutex)
                : host_(host),
                port_(port),
                telemetry_(telemetry),
                out_queue_(out_queue),
                queue_mutex_(queue_mutex),
                stop_flag_(false),
                sock_fd_(-1),
                server_fd_(-1),
                buffer_(),
                partial_line_()
            {
                setup_server();
            }

            ~SocketFeedSource() override {
                cleanup();
            }

            void run() override {
                stop_flag_.store(false, std::memory_order_release);
                
                while (!stop_flag_.load(std::memory_order_acquire)) {
                    if (sock_fd_ == -1) {
                        if (!accept_connection()) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            continue;
                        }
                    }

                    if (!read_and_process()) {
                        close_client_connection();
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            }

            void stop() override {
                stop_flag_.store(true, std::memory_order_release);
            }

            std::string source_tag() const override {
                return "SRC_SOCKET_" + host_ + ":" + std::to_string(port_);
            }

            void reset_stream() override {
                stop();
                close_client_connection();
            }

            void reset_for_restart() override {
                stop_flag_.store(false, std::memory_order_release);
                partial_line_.clear();
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
            static constexpr size_t kBufferSize = 4096;
            
            std::string host_;
            int port_;
            FeedTelemetry& telemetry_;
            std::queue<engine::Order>& out_queue_;
            std::mutex& queue_mutex_;
            std::atomic<bool> stop_flag_;
            int sock_fd_;
            int server_fd_;
            char buffer_[kBufferSize];
            std::string partial_line_;

            void setup_server() {
                server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
                if (server_fd_ < 0) {
                    throw std::runtime_error("Failed to create server socket");
                }

                int opt = 1;
                if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                    close(server_fd_);
                    throw std::runtime_error("Failed to set socket options");
                }

                struct sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = inet_addr(host_.c_str());
                addr.sin_port = htons(port_);

                if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                    close(server_fd_);
                    throw std::runtime_error("Failed to bind socket to " + host_ + ":" + std::to_string(port_));
                }

                if (listen(server_fd_, 1) < 0) {
                    close(server_fd_);
                    throw std::runtime_error("Failed to listen on socket");
                }

                // Set non-blocking for accept
                int flags = fcntl(server_fd_, F_GETFL, 0);
                fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);
            }

            bool accept_connection() {
                struct sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                
                sock_fd_ = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
                if (sock_fd_ < 0) {
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        return false; // No connection available
                    }
                    return false; // Error
                }

                // Set client socket to non-blocking
                int flags = fcntl(sock_fd_, F_GETFL, 0);
                fcntl(sock_fd_, F_SETFL, flags | O_NONBLOCK);
                
                return true;
            }

            bool read_and_process() {
                ssize_t bytes_read = recv(sock_fd_, buffer_, kBufferSize - 1, 0);
                
                if (bytes_read <= 0) {
                    if (bytes_read == 0) return false; // Connection closed
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        return true; // No data available, but connection alive
                    }
                    return false; // Error
                }

                buffer_[bytes_read] = '\0';
                std::string data = partial_line_ + std::string(buffer_, bytes_read);
                partial_line_.clear();

                size_t pos = 0;
                while (pos < data.length()) {
                    size_t newline_pos = data.find('\n', pos);
                    if (newline_pos == std::string::npos) {
                        // No complete line, save partial
                        partial_line_ = data.substr(pos);
                        break;
                    }

                    std::string line = data.substr(pos, newline_pos - pos);
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back(); // Remove CR from CRLF
                    }

                    process_line(line);
                    pos = newline_pos + 1;
                }

                return true;
            }

            void process_line(const std::string& line) {
                if (line.empty()) return;

                try {
                    engine::Order order = parse_json_order(line);
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        out_queue_.push(order);
                    }
                    telemetry_.orders_received.fetch_add(1, std::memory_order_relaxed);
                } catch (...) {
                    telemetry_.anomalies.fetch_add(1, std::memory_order_relaxed);
                }
            }

            engine::Order parse_json_order(const std::string& json_line) {
                // Simple JSON parser for: {"price":101.5,"amount":2.0,"timestamp":1725000001}
                double price = 0.0, amount = 0.0;
                int64_t timestamp = 0;

                size_t price_pos = json_line.find("\"price\":");
                size_t amount_pos = json_line.find("\"amount\":");
                size_t timestamp_pos = json_line.find("\"timestamp\":");

                if (price_pos == std::string::npos || 
                    amount_pos == std::string::npos || 
                    timestamp_pos == std::string::npos) {
                    throw std::runtime_error("Invalid JSON format");
                }

                // Extract price
                price_pos += 8; // Skip "price":
                size_t price_end = json_line.find(',', price_pos);
                if (price_end == std::string::npos) price_end = json_line.find('}', price_pos);
                price = std::stod(json_line.substr(price_pos, price_end - price_pos));

                // Extract amount
                amount_pos += 9; // Skip "amount":
                size_t amount_end = json_line.find(',', amount_pos);
                if (amount_end == std::string::npos) amount_end = json_line.find('}', amount_pos);
                amount = std::stod(json_line.substr(amount_pos, amount_end - amount_pos));

                // Extract timestamp
                timestamp_pos += 12; // Skip "timestamp":
                size_t timestamp_end = json_line.find('}', timestamp_pos);
                if (timestamp_end == std::string::npos) timestamp_end = json_line.find(',', timestamp_pos);
                timestamp = std::stoll(json_line.substr(timestamp_pos, timestamp_end - timestamp_pos));

                return engine::Order(price, amount, timestamp);
            }

            void close_client_connection() {
                if (sock_fd_ != -1) {
                    close(sock_fd_);
                    sock_fd_ = -1;
                }
            }

            void cleanup() {
                close_client_connection();
                if (server_fd_ != -1) {
                    close(server_fd_);
                    server_fd_ = -1;
                }
            }
        };

} // namespace feed
