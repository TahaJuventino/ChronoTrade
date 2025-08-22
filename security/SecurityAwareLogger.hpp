#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <openssl/sha.h>

namespace std {
    template <> struct formatter<std::thread::id> : formatter<std::string> {
        auto format(const std::thread::id &id, format_context &ctx) const {
            std::ostringstream oss;
            oss << id;
            return formatter<std::string>::format(oss.str(), ctx);
        }
    };
    } // namespace std

namespace security {

    class CryptoHasher {
        public:
            static std::string sha256(const std::string &input) {
                unsigned char hash[SHA256_DIGEST_LENGTH];
                SHA256(reinterpret_cast<const unsigned char *>(input.data()), input.size(), hash);
                std::ostringstream oss;
                for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
                }
                return oss.str();
            }
    };

    class SecurityAwareLogger {
        public:
            enum class Level { Info, Warn, Error };

            static SecurityAwareLogger &instance() {
                static SecurityAwareLogger inst;
                return inst;
            }

            template <typename... Args>
            void log(Level level, const char *fmt, Args &&...args) {
                std::string message = std::vformat(fmt, std::make_format_args(args...));
                auto now = std::chrono::system_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                auto tid = std::this_thread::get_id();
                uint64_t seq = sequence_.fetch_add(1, std::memory_order_relaxed);

                std::ostringstream meta;
                meta << seq << '|' << ms << '|' << tid << '|' << static_cast<int>(level) << '|' << message;
                std::string hash = CryptoHasher::sha256(meta.str());

                std::lock_guard<std::mutex> lock(mutex_);
                std::cerr << '[' << level_to_string(level) << "] " << message
                        << " seq=" << seq << " hash=" << hash << '\n';
            }

        private:
            SecurityAwareLogger() = default;

            const char *level_to_string(Level level) const {
                switch (level) {
                case Level::Info:
                    return "INFO";
                case Level::Warn:
                    return "WARN";
                case Level::Error:
                    return "ERROR";
                }
                return "INFO";
            }

            std::atomic<uint64_t> sequence_{0};
            std::mutex mutex_;
        };

} // namespace security