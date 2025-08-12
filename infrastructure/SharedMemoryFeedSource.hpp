#pragma once

#include "interfaces/IFeedSource.hpp"
#include "observability/FeedTelemetry.hpp"
#include "security/FeedHashLogger.hpp"
#include "core/Order.hpp"

#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <mutex>
#include <sstream>
#include <vector>
#include <fcntl.h>
#include <queue>
#ifdef __linux__
  #include <sys/mman.h>
  #include <fcntl.h>
  #include <unistd.h>
#else
  #error "SharedMemoryFeedSource only supported on Linux (use WSL or POSIX env)"
#endif

// Global constants accessible everywhere
// kMaxPackets retained for backward compatibility with tests and simulators
// but the ring buffer can now be sized dynamically via buffer_capacity_.
constexpr size_t kMaxPackets = 1024;
constexpr size_t kPacketSize = 256;

struct Packet {
    char data[kPacketSize];
    uint16_t len;
    uint16_t _padding; // Explicit padding for alignment
};

// RingBuffer now supports a dynamically sized packets array. The struct is
// allocated with enough space for `buffer_capacity_` packets.
struct RingBuffer {
    std::atomic<uint32_t> head;
    std::atomic<uint32_t> tail;
    Packet packets[1];  // Flexible array member (actual size set at runtime)
};

namespace feed {

    class SharedMemoryFeedSource : public IFeedSource {
        public:
            SharedMemoryFeedSource(const std::string& shm_name,
                                size_t buffer_capacity,
                                FeedTelemetry& telemetry,
                                std::queue<engine::Order>& out_queue,
                                std::mutex& queue_mutex)
                : shm_name_(shm_name),
                buffer_capacity_(buffer_capacity),
                telemetry_(telemetry),
                out_queue_(out_queue),
                queue_mutex_(queue_mutex),
                stop_flag_(false),
                ring_(nullptr),
                fd_(-1)
            {
                open_shm();
            }

            ~SharedMemoryFeedSource() override {
                if (ring_) munmap(ring_, ring_buffer_size());
                if (fd_ != -1) close(fd_);
            }

            void run() override {
                stop_flag_.store(false, std::memory_order_release);
                uint32_t local_tail = ring_->tail.load(std::memory_order_acquire);
                
                while (!stop_flag_.load(std::memory_order_acquire)) {
                    uint32_t head = ring_->head.load(std::memory_order_acquire);

                    while (local_tail != head) {
                        const Packet& pkt = ring_->packets[local_tail % buffer_capacity_];
                        
                        if (hash_logger_) {
                            hash_logger_->log_packet(pkt.data, pkt.len, "SHM");
                        }

                        if (!is_ascii(pkt.data, pkt.len)) {
                            telemetry_.anomalies.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            try {
                                engine::Order o = parse(pkt);
                                {
                                    std::lock_guard<std::mutex> lock(queue_mutex_);
                                    out_queue_.push(o);
                                }
                                telemetry_.orders_received.fetch_add(1, std::memory_order_relaxed);
                            } catch (...) {
                                telemetry_.anomalies.fetch_add(1, std::memory_order_relaxed);
                            }
                        }

                        local_tail = (local_tail + 1) % buffer_capacity_;
                        // Update shared tail after processing
                        ring_->tail.store(local_tail, std::memory_order_release);
                    }

                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            }

            void stop() override {
                stop_flag_.store(true, std::memory_order_release);
            }

            std::string source_tag() const override {
                return "SRC_SHM_" + shm_name_;
            }

            void reset_stream() override { 
                stop(); 
            }

            void reset_for_restart() override { 
                stop_flag_.store(false, std::memory_order_release); 
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

            void set_hash_logger(std::unique_ptr<security::FeedHashLogger> logger) {
                hash_logger_ = std::move(logger);
            }


        private:
            std::string shm_name_;
            size_t buffer_capacity_;
            FeedTelemetry& telemetry_;

            std::queue<engine::Order>& out_queue_;
            std::mutex& queue_mutex_;
            std::atomic<bool> stop_flag_;
            std::unique_ptr<security::FeedHashLogger> hash_logger_;

            RingBuffer* ring_;
            int fd_;

            void open_shm() {
                fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
                if (fd_ < 0) throw std::runtime_error("Failed to open SHM: " + shm_name_);

                size_t sz = ring_buffer_size();
                if (ftruncate(fd_, sz) != 0)
                    throw std::runtime_error("ftruncate failed");

                void* ptr = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
                if (ptr == MAP_FAILED) throw std::runtime_error("mmap failed");

                ring_ = static_cast<RingBuffer*>(ptr);
            }

            size_t ring_buffer_size() const {
                return sizeof(RingBuffer) + (buffer_capacity_ - 1) * sizeof(Packet);
            }

            engine::Order parse(const Packet& pkt) {
                std::string line(pkt.data, pkt.len);
                std::istringstream ss(line);
                std::string token;
                std::vector<std::string> parts;
                while (std::getline(ss, token, ',')) {
                    parts.push_back(token);
                }

                if (parts.size() != 3) throw std::runtime_error("Malformed CSV packet");

                double price = std::stod(parts[0]);
                double amount = std::stod(parts[1]);
                int64_t ts = std::stoll(parts[2]);

                return engine::Order(price, amount, ts);
            }

            bool is_ascii(const char* data, size_t len) {
                for (size_t i = 0; i < len; ++i) {
                    if (data[i] < 32 || data[i] > 126) return false;
                }
                return true;
            }
    };

}  // namespace feed
