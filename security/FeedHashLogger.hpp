#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>  // Requires linking OpenSSL
#include <filesystem>  // C++17

namespace security {

    class FeedHashLogger {
        public:
            static std::string compute_sha256(const std::string& input) {
                unsigned char hash[SHA256_DIGEST_LENGTH];
                SHA256(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), hash);

                std::ostringstream oss;
                for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
                    oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
                return oss.str();
            }

            // Accepts raw buffer
            static void log_packet(const char* data, uint16_t len, const std::string& tag) {
                ensure_log_dir();
                std::string view(data, len);
                std::string hash = compute_sha256(view);
                std::lock_guard<std::mutex> lock(file_mutex_);
                std::ofstream log("logs/feed_hash.log", std::ios::app);
                log << "[FEED] [" << tag << "] SHA256=" << hash << " line=" << view << "\n";
            }

            // Accepts 3 precomputed string args
            static void log_packet(const std::string& line, const std::string& hash, const std::string& tag) {
                ensure_log_dir();
                std::lock_guard<std::mutex> lock(file_mutex_);
                std::ofstream log("logs/feed_hash.log", std::ios::app);
                log << "[FEED] [" << tag << "] SHA256=" << hash << " line=" << line << "\n";
            }

            static void log_anomaly(const std::string& expected, const std::string& actual, const std::string& tag) {
                ensure_log_dir();
                std::lock_guard<std::mutex> lock(file_mutex_);
                std::ofstream log("logs/feed_hash.log", std::ios::app);
                log << "[ANOMALY] [" << tag << "] Expected=" << expected
                    << " Got=" << actual << "\n";
            }

            static void ensure_log_dir() {
                std::filesystem::create_directories("logs");
            }

            static void log_and_verify(const std::string& line, const std::string& parsed_repr, const std::string& tag) {
                std::string original_hash = compute_sha256(line);
                std::string parsed_hash = compute_sha256(parsed_repr);

                log_packet(line, original_hash, tag);
                if (original_hash != parsed_hash) {
                    log_anomaly(original_hash, parsed_hash, tag);
                }
            }
            
        private:
            static inline std::mutex file_mutex_;
    };

}  // namespace security
