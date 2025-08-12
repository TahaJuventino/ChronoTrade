#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <climits>
#include <algorithm>
#include <netinet/in.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

using std::chrono::steady_clock;
using std::chrono::seconds;
using std::chrono::milliseconds;

struct ProxyConfig {
    std::string listen_host = "127.0.0.1";
    int listen_port = 7001;
    std::string upstream_host = "127.0.0.1";
    int upstream_port = 7002;

    int latency_ms = 0;
    int jitter_ms = 0;
    double drop_rate = 0.0;
    double dup_rate = 0.0;
    int bandwidth_kbps = 0;
    size_t buffer_bytes = 4096;
    std::string direction = "both";
    int max_connections = 128;
    bool half_close = true;
    bool enable_burst = false;
    bool http_friendly_errors = false;
    bool rst_on_upstream_fail = false;
    int socket_timeout_sec = 10;
    bool verbose = false;
    uint32_t seed = 0;
};

// Precision bandwidth throttle with burst support
class PrecisionBandwidthThrottle {
    int64_t kbps_;
    bool enable_burst_;
    std::mutex mu_;
    std::chrono::steady_clock::time_point last_;
    int64_t tokens_;          // bytes
    int64_t max_tokens_;      // bytes
    int64_t num_bytes_per_s_; // = kbps * 1000 / 8
    int64_t frac_acc_;        // numerator accumulator (bytes * 1e6)

public:
    PrecisionBandwidthThrottle(int kbps, bool burst, bool verbose = false)
      : kbps_(kbps), enable_burst_(burst), last_(std::chrono::steady_clock::now()),
        num_bytes_per_s_((int64_t)kbps * 1000 / 8),
        max_tokens_(std::max<int64_t>(1, num_bytes_per_s_ * (burst ? 2 : 1))),
        tokens_(burst ? max_tokens_ : 0), frac_acc_(0) {
        
        if (verbose) {
            std::cerr << "[DEBUG] PrecisionBandwidthThrottle: " << kbps << " kbps = " 
                      << num_bytes_per_s_ << " bytes/sec, max_tokens=" << max_tokens_ 
                      << ", burst=" << burst << std::endl;
        }
    }

    void throttle(size_t bytes) {
        if (kbps_ <= 0 || bytes == 0) return;
        int64_t need = (int64_t)bytes;

        for (;;) {
            int64_t deficit;
            {
                std::lock_guard<std::mutex> lk(mu_);
                auto now = std::chrono::steady_clock::now();
                auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_).count();
                if (dt_us > 0) {
                    // Add floor(bytes_per_s * dt_us / 1e6) using accumulator to keep precision
#if defined(__SIZEOF_INT128__)
                    __int128 add_num = (__int128)num_bytes_per_s_ * dt_us + frac_acc_;
                    int64_t add_bytes = (int64_t)(add_num / 1'000'000);
                    frac_acc_ = (int64_t)(add_num % 1'000'000);
#else
                    // Fallback for platforms without __int128
                    int64_t add_bytes = (num_bytes_per_s_ * dt_us) / 1'000'000;
                    int64_t frac_part = (num_bytes_per_s_ * dt_us) % 1'000'000;
                    frac_acc_ += frac_part;
                    if (frac_acc_ >= 1'000'000) {
                        add_bytes += frac_acc_ / 1'000'000;
                        frac_acc_ %= 1'000'000;
                    }
#endif
                    tokens_ = std::min(max_tokens_, tokens_ + add_bytes);
                    last_ = now;
                }
                deficit = need - tokens_;
                if (deficit <= 0) { tokens_ -= need; return; }
            }
            // sleep for the missing bytes
            // time = deficit * 1e6 / bytes_per_s
            int64_t us = std::max<int64_t>(1000, std::min<int64_t>(100000,  // Increased max sleep from 20ms to 100ms
                            (deficit * 1'000'000) / std::max<int64_t>(1, num_bytes_per_s_)));
            std::this_thread::sleep_for(std::chrono::microseconds(us));
        }
    }

    size_t burst_capacity() const { 
        return (size_t)std::min<int64_t>(max_tokens_, 8192); 
    }

    size_t bytes_per_100ms() const {
        return (size_t)std::max<int64_t>(1, num_bytes_per_s_ / 10);
    }

    // Returns how many bytes can be sent immediately without blocking.
    // Does NOT consume tokens.
    size_t allowance(size_t max_bytes = 8192) {
        if (kbps_ <= 0) return max_bytes; // unlimited
        std::lock_guard<std::mutex> lk(mu_);
        auto now = std::chrono::steady_clock::now();
        auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_).count();
        if (dt_us > 0) {
    #if defined(__SIZEOF_INT128__)
            __int128 add_num = (__int128)num_bytes_per_s_ * dt_us + frac_acc_;
            int64_t add_bytes = (int64_t)(add_num / 1'000'000);
            frac_acc_ = (int64_t)(add_num % 1'000'000);
    #else
            int64_t add_bytes = (num_bytes_per_s_ * dt_us) / 1'000'000;
            int64_t frac_part = (num_bytes_per_s_ * dt_us) % 1'000'000;
            frac_acc_ += frac_part;
            if (frac_acc_ >= 1'000'000) {
                add_bytes += frac_acc_ / 1'000'000;
                frac_acc_ %= 1'000'000;
            }
    #endif
            tokens_ = std::min(max_tokens_, tokens_ + add_bytes);
            last_ = now;
        }
        int64_t avail = std::max<int64_t>(0, tokens_);
        size_t allow = (size_t)std::min<int64_t>(avail, (int64_t)max_bytes);
        return std::max<size_t>(1, allow);
    }

};

static std::atomic<int> g_accept_backpressure_ms{0};

struct ConnThread {
    std::thread th;
    std::shared_ptr<std::atomic<bool>> done;
};
std::vector<ConnThread> connection_threads;

static std::atomic<bool> g_running{true};

// Connection stats for monitoring
struct ConnectionStats {
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> packets_dropped{0};
    std::atomic<uint64_t> packets_duplicated{0};
    std::atomic<uint64_t> last_activity_us{0};
    std::chrono::steady_clock::time_point start_time;
    
    ConnectionStats() : start_time(std::chrono::steady_clock::now()) {
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        last_activity_us.store(now_us, std::memory_order_relaxed);
    }
    
    void update_activity() {
        auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        last_activity_us.store(now_us, std::memory_order_relaxed);
    }
    
    std::chrono::steady_clock::time_point get_last_activity() const {
        auto us = last_activity_us.load(std::memory_order_relaxed);
        return std::chrono::steady_clock::time_point(std::chrono::microseconds(us));
    }
    
    void print_stats(const char* direction) const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        if (seconds > 0) {
            double throughput_kbps = (bytes_sent.load() * 8.0) / (seconds * 1000.0);
            std::cerr << "[STATS] " << direction << ": " << bytes_sent.load() << " bytes sent, "
                      << bytes_received.load() << " bytes received, "
                      << throughput_kbps << " kbps avg, " << packets_dropped.load() << " dropped, "
                      << packets_duplicated.load() << " duplicated" << std::endl;
        }
    }
};

void sigint_handler(int) {
    g_running = false;
}

static void usage(const char* argv0) {
    std::cerr <<
        "Usage: " << argv0 << " \\\n"
        "  --listen-host 0.0.0.0 --listen-port 9000 \\\n"
        "  --upstream-host 127.0.0.1 --upstream-port 7000 \\\n"
        "  [--latency-ms 50] [--jitter-ms 10] [--drop-rate 0.05] [--dup-rate 0.01] \\\n"
        "  [--bandwidth-kbps 256] [--buffer-bytes 8192] [--direction up|down|both] \\\n"
        "  [--max-connections 128] [--no-half-close] [--half-close] [--enable-burst] \\\n"
        "  [--http-friendly-errors] [--rst-on-upstream-fail] [--socket-timeout-sec 10] \\\n"
        "  [--verbose] [--seed 1234]\n";
}

static bool parse_int(const char* s, int& out) {
    char* end=nullptr; long v = std::strtol(s, &end, 10);
    if (!s || *s=='\0' || (end && *end!='\0')) return false;
    out = static_cast<int>(v); return true;
}

static bool parse_size(const char* s, size_t& out) {
    char* end=nullptr; long long v = std::strtoll(s, &end, 10);
    if (!s || *s=='\0' || (end && *end!='\0') || v<0) return false;
    out = static_cast<size_t>(v); return true;
}

static bool parse_double(const char* s, double& out) {
    char* end=nullptr; double v = std::strtod(s, &end);
    if (!s || *s=='\0' || (end && *end!='\0')) return false;
    out = v; return true;
}

bool parse_args(int argc, char** argv, ProxyConfig& cfg) {
    for (int i=1;i<argc;i++) {
        std::string a = argv[i];
        auto next = [&](int& target){ if (i+1>=argc) return false; return parse_int(argv[++i], target); };
        auto nextsz = [&](size_t& target){ if (i+1>=argc) return false; return parse_size(argv[++i], target); };
        auto nextd = [&](double& target){ if (i+1>=argc) return false; return parse_double(argv[++i], target); };

        if (a=="--listen-host")      { if (++i>=argc) return false; cfg.listen_host = argv[i]; }
        else if (a=="--listen-port") { if (!next(cfg.listen_port)) return false; }
        else if (a=="--upstream-host"){ if (++i>=argc) return false; cfg.upstream_host = argv[i]; }
        else if (a=="--upstream-port"){ if (!next(cfg.upstream_port)) return false; }
        else if (a=="--latency-ms")  { if (!next(cfg.latency_ms)) return false; }
        else if (a=="--jitter-ms")   { if (!next(cfg.jitter_ms)) return false; }
        else if (a=="--drop-rate")   { if (!nextd(cfg.drop_rate)) return false; }
        else if (a=="--dup-rate")    { if (!nextd(cfg.dup_rate)) return false; }
        else if (a=="--bandwidth-kbps"){ if (!next(cfg.bandwidth_kbps)) return false; }
        else if (a=="--buffer-bytes"){ if (!nextsz(cfg.buffer_bytes)) return false; }
        else if (a=="--direction")   { if (++i>=argc) return false; cfg.direction = argv[i]; }
        else if (a=="--max-connections"){ if (!next(cfg.max_connections)) return false; }
        else if (a=="--no-half-close"){ cfg.half_close = false; }
        else if (a=="--half-close")  { cfg.half_close = true; }
        else if (a=="--enable-burst"){ cfg.enable_burst = true; }
        else if (a=="--http-friendly-errors"){ cfg.http_friendly_errors = true; }
        else if (a=="--rst-on-upstream-fail"){ cfg.rst_on_upstream_fail = true; }
        else if (a=="--socket-timeout-sec"){ if (!next(cfg.socket_timeout_sec)) return false; }
        else if (a=="--verbose" || a=="-v"){ cfg.verbose = true; }
        else if (a=="--seed")        { int s; if (!next(s)) return false; cfg.seed = static_cast<uint32_t>(s); }
        else { usage(argv[0]); return false; }
    }
    if (cfg.drop_rate < 0.0) cfg.drop_rate = 0.0;
    if (cfg.drop_rate > 1.0) cfg.drop_rate = 1.0;
    if (cfg.dup_rate < 0.0) cfg.dup_rate = 0.0;
    if (cfg.dup_rate > 1.0) cfg.dup_rate = 1.0;
    if (cfg.jitter_ms < 0) cfg.jitter_ms = 0;
    if (cfg.latency_ms < 0) cfg.latency_ms = 0;
    if (cfg.bandwidth_kbps < 0) cfg.bandwidth_kbps = 0;
    if (cfg.socket_timeout_sec < 1) cfg.socket_timeout_sec = 1;
    if (cfg.socket_timeout_sec > 300) cfg.socket_timeout_sec = 300;
    return true;
}

// Check if data looks like HTTP request or response
static bool looks_like_http(const char* data, size_t len) {
    if (!data || len == 0) return false;
    constexpr size_t kCap = 256;
    size_t i = 0;
    while (i < len && i < kCap) {
        char c = data[i];
        if (c!=' ' && c!='\t' && c!='\r' && c!='\n' && c!='\0') break;
        ++i;
    }
    if (len - i < 3) return false;
    auto is = [&](const char* s, size_t n){
        if (len - i < n) return false;
        for (size_t k=0;k<n;++k){ char a=data[i+k]; if(a>='a'&&a<='z') a-=32; if(a!=s[k]) return false; }
        return true;
    };
    return is("GET ",4)||is("POST ",5)||is("PUT ",4)||is("HEAD ",5)||
           is("DELETE ",7)||is("PATCH ",6)||is("OPTIONS ",8)||is("CONNECT ",8)||
           is("TRACE ",6)||is("HTTP/",5);
}

static void send_http_503(int client_fd) {
    const char* response = 
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 19\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Service Unavailable";
    
    send(client_fd, response, strlen(response), MSG_NOSIGNAL);
}

static void set_socket_timeouts(int fd, int timeout_sec = 10) {
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    int keepalive = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
}

static int dial(const std::string& host, int port) {
    addrinfo hints{}, *res=nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_s = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
    if (rc != 0) {
        std::cerr << "[ERROR] getaddrinfo(" << host << ":" << port
                  << "): " << gai_strerror(rc) << std::endl;
        return -1;
    }

    int fd = -1;
    for (auto p=res; p; p=p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        set_socket_timeouts(fd, 5);
        
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd); fd = -1;
    }
    freeaddrinfo(res);
    
    if (fd < 0) {
        std::cerr << "[ERROR] connect to " << host << ":" << port
                  << " failed: " << strerror(errno) << std::endl;
    } else {
        set_socket_timeouts(fd, 10);
    }
    
    return fd;
}

int listen_on(const std::string& host, int port, int backlog) {
    addrinfo hints{}, *res=nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    std::string port_s = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
    if (rc != 0) {
        std::cerr << "[ERROR] getaddrinfo(listen " << host << ":" << port
                  << "): " << gai_strerror(rc) << std::endl;
        return -1;
    }

    int fd = -1;
    for (auto p=res; p; p=p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        
        int yes=1; 
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        
        if (p->ai_family == AF_INET6) {
            int v6only = 0;
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) != 0) {
                std::cerr << "[WARN] Could not enable dual-stack, continuing with IPv6-only" << std::endl;
            }
        }
        
        if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0 && ::listen(fd, backlog) == 0) {
            break;
        }
        ::close(fd); fd = -1;
    }
    freeaddrinfo(res);
    
    if (fd < 0) {
        std::cerr << "[ERROR] Failed to bind/listen on " << host << ":" << port 
                  << ": " << strerror(errno) << std::endl;
    }

    return fd;
}

struct DirectionFlags {
    bool up = true;
    bool down = true;
};

static DirectionFlags parse_direction(const std::string& d) {
    DirectionFlags f{};
    if (d=="up")   { f.down = false; }
    else if (d=="down") { f.up = false; }
    else if (d=="both") { /* default */ }
    else { throw std::invalid_argument("Invalid --direction (expected up|down|both)"); }
    return f;
}

static void sleep_with_latency(std::mt19937& rng, int base_ms, int jitter_ms) {
    int jitter = 0;
    if (jitter_ms > 0) {
        std::uniform_int_distribution<int> dist(-jitter_ms, jitter_ms);
        jitter = dist(rng);
    }
    int delay = std::max(0, base_ms + jitter);
    if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
}

// Simpler data processor that handles both bulk and line data properly
class DataProcessor {
    std::string pending_;
    const size_t MAX_PENDING = 64 * 1024;
    
public:
    template<typename PacketHandler>
    bool process_data(const char* data, size_t len, PacketHandler handler) {
        // For very small amounts of data or when we already have pending data,
        // try to accumulate lines. Otherwise, process in chunks.
        
        if (len <= 1024 || !pending_.empty()) {
            // Line-based processing for small data
            return process_lines(data, len, handler);
        } else {
            // Chunk-based processing for bulk data
            return process_chunks(data, len, handler);
        }
    }
    
    template<typename PacketHandler>
    bool flush_pending(PacketHandler handler) {
        if (!pending_.empty()) {
            if (!handler(pending_.c_str(), pending_.size())) return false;
            pending_.clear();
        }
        return true;
    }
    
private:
    template<typename PacketHandler>
    bool process_lines(const char* data, size_t len, PacketHandler handler) {
        size_t pos = 0;
        
        while (pos < len) {
            size_t nl_pos = std::string::npos;
            
            // Look for newline in current chunk
            for (size_t i = pos; i < len; ++i) {
                if (data[i] == '\n') {
                    nl_pos = i;
                    break;
                }
            }
            
            if (nl_pos != std::string::npos) {
                // Found complete line
                size_t chunk_len = nl_pos - pos + 1;  // include '\n'
                
                if (!pending_.empty()) {
                    // Combine pending + chunk
                    pending_.append(data + pos, chunk_len);
                    if (pending_.size() <= MAX_PENDING) {
                        if (!handler(pending_.c_str(), pending_.size())) return false;
                    }
                    pending_.clear();
                } else {
                    // Direct chunk
                    if (!handler(data + pos, chunk_len)) return false;
                }
                
                pos = nl_pos + 1;
            } else {
                // No newline found, append to pending
                size_t remaining = len - pos;
                
                if (pending_.size() + remaining <= MAX_PENDING) {
                    pending_.append(data + pos, remaining);
                } else if (pending_.empty()) {
                    // Single chunk too big, send as-is
                    if (!handler(data + pos, remaining)) return false;
                } else {
                    // Pending + chunk too big, flush pending and start fresh
                    if (!handler(pending_.c_str(), pending_.size())) return false;
                    pending_.clear();
                    
                    if (remaining <= MAX_PENDING) {
                        pending_.append(data + pos, remaining);
                    } else {
                        if (!handler(data + pos, remaining)) return false;
                    }
                }
                break;
            }
        }
        return true;
    }
    
    template<typename PacketHandler>
    bool process_chunks(const char* data, size_t len, PacketHandler handler) {
        // Process in reasonable chunks for bulk data
        constexpr size_t CHUNK_SIZE = 1400; // Close to MTU size
        size_t pos = 0;
        
        while (pos < len) {
            size_t chunk_size = std::min(CHUNK_SIZE, len - pos);
            if (!handler(data + pos, chunk_size)) return false;
            pos += chunk_size;
        }
        return true;
    }
};

// Improved bandwidth throttling with better small-data handling
static bool safe_send_all(int fd, const char* data, size_t len,
                          PrecisionBandwidthThrottle* throttle = nullptr,
                          bool has_impairments = false) {
    if (len == 0) return true;

    // For very small data (like individual lines), send immediately if no throttling
    if (len <= 64 && (!throttle || throttle->allowance(len) >= len)) {
        ssize_t n = ::send(fd, data, len, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) return safe_send_all(fd, data, len, throttle, has_impairments);
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                return safe_send_all(fd, data, len, throttle, has_impairments);
            }
            return false;
        }
        if (throttle) throttle->throttle(n); // Consume tokens after successful send
        return static_cast<size_t>(n) == len;
    }

    // Adaptive batch sizing
    const size_t kCleanBatch = 1200;
    const size_t kImpairBatch = 512;
    const size_t kMaxWrite = has_impairments ? kImpairBatch : kCleanBatch;

    size_t sent = 0;
    while (sent < len) {    
        size_t chunk_size = std::min(len - sent, kMaxWrite);

        if (throttle) {
            // Cap to reasonable amount for rate limiting precision
            chunk_size = std::min(chunk_size, std::max<size_t>(1, throttle->bytes_per_100ms()));

            size_t slice = throttle->allowance(chunk_size);
            if (slice == 0) slice = 1;
            throttle->throttle(slice);
            chunk_size = slice;
        }

        ssize_t n = ::send(fd, data + sent, chunk_size, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            std::cerr << "[ERROR] send failed: " << strerror(errno) << std::endl;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

static void proxy_pipe(int from_fd, int to_fd, ProxyConfig cfg, bool enabled,
                       std::atomic<bool>& stop_flag, const char* tag, ConnectionStats& stats) {

    const size_t safe_buffer_size = std::min(cfg.buffer_bytes, static_cast<size_t>(INT_MAX));
    std::vector<char> buf(safe_buffer_size);

    uint32_t connection_seed = cfg.seed;
    if (cfg.seed != 0) {
        connection_seed = cfg.seed ^ static_cast<uint32_t>(from_fd) ^ static_cast<uint32_t>(to_fd);
    }
    std::mt19937 rng(connection_seed ? connection_seed : std::random_device{}());
    std::uniform_real_distribution<double> U(0.0, 1.0);

    std::unique_ptr<PrecisionBandwidthThrottle> throttle;
    if (enabled && cfg.bandwidth_kbps > 0) {
        throttle = std::make_unique<PrecisionBandwidthThrottle>(cfg.bandwidth_kbps, cfg.enable_burst, cfg.verbose);
        if (cfg.verbose) {
            std::cerr << "[DEBUG] " << tag << ": precision bandwidth throttling enabled at "
                      << cfg.bandwidth_kbps << " kbps (burst=" << cfg.enable_burst << ")\n";
        }
    }

    // Only use processor when impairments are active
    DataProcessor processor;
    bool need_processor = enabled && (cfg.latency_ms > 0 || cfg.drop_rate > 0.0 || cfg.dup_rate > 0.0);
    bool has_impairments = need_processor;

    while (g_running && !stop_flag.load()) {
        // Bound read size based on bandwidth allowance BEFORE recv()
        size_t want = safe_buffer_size;
        if (enabled && throttle) {
            want = std::min(want, throttle->allowance(8192));
        }

        ssize_t n = ::recv(from_fd, buf.data(), static_cast<int>(want), 0);

        if (n == 0) {
            // Handle pending data on EOF
            if (need_processor) {
                bool flush_ok = processor.flush_pending([&](const char* chunk_data, size_t chunk_size) -> bool {
                    if (!safe_send_all(to_fd, chunk_data, chunk_size, throttle.get(), has_impairments)) return false;
                    stats.bytes_sent.fetch_add(chunk_size, std::memory_order_relaxed);
                    return true;
                });
                if (!flush_ok) break;
            }
            
            if (cfg.half_close) {
                ::shutdown(to_fd, SHUT_WR);
            }
            break;
        }

        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            ::shutdown(to_fd, SHUT_WR);
            break;
        }

        stats.bytes_received.fetch_add(static_cast<size_t>(n), std::memory_order_relaxed);
        stats.update_activity();

        // Fast path: no impairments except throttling
        if (!enabled || !need_processor) {
            if (!safe_send_all(to_fd, buf.data(), static_cast<size_t>(n), throttle.get(), has_impairments)) {
                ::shutdown(to_fd, SHUT_WR);
                break;
            }
            stats.bytes_sent.fetch_add(static_cast<size_t>(n), std::memory_order_relaxed);
            stats.update_activity();
            continue;
        }

        // Impairments path: use processor to handle data properly
        bool process_ok = processor.process_data(buf.data(), static_cast<size_t>(n), 
            [&](const char* chunk_data, size_t chunk_size) -> bool {
                // Drop simulation
                if (cfg.drop_rate > 0.0 && U(rng) < cfg.drop_rate) {
                    stats.packets_dropped.fetch_add(1, std::memory_order_relaxed);
                    if (cfg.latency_ms > 0 || cfg.jitter_ms > 0)
                        sleep_with_latency(rng, cfg.latency_ms, cfg.jitter_ms);
                    return true;
                }

                // Latency + jitter
                if (cfg.latency_ms > 0 || cfg.jitter_ms > 0)
                    sleep_with_latency(rng, cfg.latency_ms, cfg.jitter_ms);

                // Send original
                if (chunk_size > 0 && !safe_send_all(to_fd, chunk_data, chunk_size, throttle.get(), has_impairments)) {
                    return false;
                }

                if (chunk_size > 0) {
                    stats.bytes_sent.fetch_add(chunk_size, std::memory_order_relaxed);
                    stats.update_activity();
                }

                // Possibly duplicate
                if (chunk_size > 0 && cfg.dup_rate > 0.0 && U(rng) < cfg.dup_rate) {
                    stats.packets_duplicated.fetch_add(1, std::memory_order_relaxed);
                    if (cfg.latency_ms > 0 || cfg.jitter_ms > 0)
                        sleep_with_latency(rng, cfg.latency_ms / 4, cfg.jitter_ms / 4);
                    if (!safe_send_all(to_fd, chunk_data, chunk_size, throttle.get(), has_impairments)) {
                        return false;
                    }
                    stats.bytes_sent.fetch_add(chunk_size, std::memory_order_relaxed);
                    stats.update_activity();
                }
                return true;
            });
        
        if (!process_ok) {
            ::shutdown(to_fd, SHUT_WR);
            break;
        }
    }
}

void handle_connection(int client_fd, const ProxyConfig& cfg) {
    char client_ip[INET6_ADDRSTRLEN + 3] = {};
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    
    if (getpeername(client_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0) {
        if (addr.ss_family == AF_INET) {
            auto* a = reinterpret_cast<sockaddr_in*>(&addr);
            inet_ntop(AF_INET, &a->sin_addr, client_ip, sizeof(client_ip));
        } else if (addr.ss_family == AF_INET6) {
            auto* a = reinterpret_cast<sockaddr_in6*>(&addr);
            char ip_raw[INET6_ADDRSTRLEN] = {};
            inet_ntop(AF_INET6, &a->sin6_addr, ip_raw, sizeof(ip_raw));
            std::snprintf(client_ip, sizeof(client_ip), "[%s]", ip_raw);
        }
    }

    if (cfg.verbose) {
        std::cerr << "[INFO] New connection from " << client_ip 
                  << ", connecting to upstream " << cfg.upstream_host << ":" << cfg.upstream_port << std::endl;
    }

    int upstream_fd = dial(cfg.upstream_host, cfg.upstream_port);

    if (upstream_fd < 0) {
        std::cerr << "[ERROR] Failed to connect to upstream" << std::endl;

        if (cfg.http_friendly_errors) {
            struct timeval peek_tv{0};
            peek_tv.tv_usec = 100000;  // 100ms
            struct timeval orig_tv{0};
            socklen_t orig_tv_len = sizeof(orig_tv);
            
            getsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &orig_tv, &orig_tv_len);
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &peek_tv, sizeof(peek_tv));
            
            char peek_buf[8] = {};
            ssize_t peeked = recv(client_fd, peek_buf, sizeof(peek_buf), MSG_PEEK);
            if (peeked > 0 && looks_like_http(peek_buf, static_cast<size_t>(peeked))) {
                send_http_503(client_fd);
                ::shutdown(client_fd, SHUT_WR);
                
                char tmp[1024];
                struct timeval drain_tv{0};
                drain_tv.tv_usec = 200000;  // 200ms
                setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &drain_tv, sizeof(drain_tv));
                while (recv(client_fd, tmp, sizeof(tmp), 0) > 0) {}
                
                ::close(client_fd);
                return;
            }
            
            setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &orig_tv, sizeof(orig_tv));
        }

        if (cfg.rst_on_upstream_fail) {
            struct linger lin; 
            lin.l_onoff = 1; 
            lin.l_linger = 0;
            setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        }
        
        ::close(client_fd);
        return;
    }

    DirectionFlags dirs = parse_direction(cfg.direction);
    std::atomic<bool> stop{false};
    
    ConnectionStats up_stats, down_stats;

    std::thread t_up([&]{
        proxy_pipe(client_fd, upstream_fd, cfg, dirs.up, stop, "UP", up_stats);
        stop = true;
    });
    std::thread t_down([&]{
        proxy_pipe(upstream_fd, client_fd, cfg, dirs.down, stop, "DOWN", down_stats);
        stop = true;
    });

    const auto THREAD_TIMEOUT = seconds(300);

    while (!stop.load() && g_running.load()) {
        auto now = steady_clock::now();
        
        auto up_last = up_stats.get_last_activity();
        auto down_last = down_stats.get_last_activity();
        auto most_recent = std::max(up_last, down_last);
        
        if (now - most_recent > THREAD_TIMEOUT) {
            std::cerr << "[WARN] Connection idle timeout (" 
                      << std::chrono::duration_cast<seconds>(now - most_recent).count() 
                      << "s), forcing close" << std::endl;
            stop = true; 
            break;
        }
        std::this_thread::sleep_for(milliseconds(100));
    }

    stop = true;

    if (upstream_fd >= 0) { ::shutdown(upstream_fd, SHUT_RDWR); ::close(upstream_fd); }
    if (client_fd >= 0) { ::shutdown(client_fd, SHUT_RDWR); ::close(client_fd); }

    if (t_up.joinable()) t_up.join();
    if (t_down.joinable()) t_down.join();

    auto total_elapsed = steady_clock::now() - up_stats.start_time;
    auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_elapsed).count();
    if (total_seconds > 0) {
        uint64_t total_sent = up_stats.bytes_sent.load() + down_stats.bytes_sent.load();
        uint64_t total_received = up_stats.bytes_received.load() + down_stats.bytes_received.load();
        uint64_t total_dropped = up_stats.packets_dropped.load() + down_stats.packets_dropped.load();
        uint64_t total_duped = up_stats.packets_duplicated.load() + down_stats.packets_duplicated.load();
        double avg_throughput_kbps = (total_sent * 8.0) / (total_seconds * 1000.0);
        
        std::cerr << "[STATS] Connection summary: " << total_sent << " bytes sent, "
                  << total_received << " bytes received, " << avg_throughput_kbps << " kbps avg, "
                  << total_dropped << " dropped, " << total_duped << " duplicated" << std::endl;
    }

    if (cfg.verbose) {
        std::cerr << "[INFO] Connection from " << client_ip << " closed" << std::endl;
    }
}

int main(int argc, char** argv) {
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);
    std::signal(SIGPIPE, SIG_IGN);

    ProxyConfig cfg;
    if (!parse_args(argc, argv, cfg)) {
        usage(argv[0]);
        return 1;
    }

    std::cerr << "[CONFIG] Listen: " << cfg.listen_host << ":" << cfg.listen_port << std::endl;
    std::cerr << "[CONFIG] Upstream: " << cfg.upstream_host << ":" << cfg.upstream_port << std::endl;
    std::cerr << "[CONFIG] Latency: " << cfg.latency_ms << "ms ± " << cfg.jitter_ms << "ms" << std::endl;
    std::cerr << "[CONFIG] Drop rate: " << cfg.drop_rate << ", Dup rate: " << cfg.dup_rate << std::endl;
    std::cerr << "[CONFIG] Direction: " << cfg.direction << ", Bandwidth: " << cfg.bandwidth_kbps << " kbps" << std::endl;
    std::cerr << "[CONFIG] Burst mode: " << (cfg.enable_burst ? "enabled" : "disabled") << std::endl;
    std::cerr << "[CONFIG] HTTP errors: " << (cfg.http_friendly_errors ? "enabled" : "disabled") << std::endl;
    std::cerr << "[CONFIG] RST on upstream fail: " << (cfg.rst_on_upstream_fail ? "enabled" : "disabled") << std::endl;
    std::cerr << "[CONFIG] Socket timeout: " << cfg.socket_timeout_sec << "s" << std::endl;
    std::cerr << "[CONFIG] Verbose: " << (cfg.verbose ? "enabled" : "disabled") << std::endl;

    if (cfg.listen_port <= 0 || cfg.listen_port > 65535) {
        std::cerr << "[FATAL] Invalid --listen-port\n"; 
        return 1;
    }
    if (cfg.upstream_port <= 0 || cfg.upstream_port > 65535) {
        std::cerr << "[FATAL] Invalid --upstream-port\n"; 
        return 1;
    }
    if (cfg.buffer_bytes == 0) {
        std::cerr << "[WARN] buffer-bytes=0, forcing 4096\n";
        cfg.buffer_bytes = 4096;
    }

    try {
        (void)parse_direction(cfg.direction);
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    int listen_fd = listen_on(cfg.listen_host, cfg.listen_port, cfg.max_connections);
    if (listen_fd < 0) {
        std::cerr << "[FATAL] Failed to bind to " << cfg.listen_host << ":" << cfg.listen_port << std::endl;
        return 1;
    }

    std::cerr << "[INFO] Proxy listening on " << cfg.listen_host << ":" << cfg.listen_port << std::endl;

    std::mutex threads_mutex;
    
    std::thread cleanup_thread([&]() {
        using namespace std::chrono_literals;
        while (g_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(250ms);
            std::lock_guard<std::mutex> lock(threads_mutex);

            for (auto it = connection_threads.begin(); it != connection_threads.end(); ) {
                if (it->done->load(std::memory_order_acquire)) {
                    if (it->th.joinable()) it->th.join();
                    it = connection_threads.erase(it);
                } else {
                    ++it;
                }
            }
        }
        std::lock_guard<std::mutex> lock(threads_mutex);
        for (auto &h : connection_threads) {
            if (h.th.joinable()) h.th.join();
        }
        connection_threads.clear();
    });
  
    while (g_running.load()) {
        sockaddr_storage client_addr{};
        socklen_t client_len = sizeof(client_addr);

        #ifdef __linux__
            int client_fd = accept4(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len, SOCK_CLOEXEC);
        #else
            int client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd >= 0) {
                fcntl(client_fd, F_SETFD, FD_CLOEXEC);
            }
        #endif

        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (!g_running.load()) break;
            std::cerr << "[ERROR] accept failed: " << strerror(errno) << "\n";
            continue;
        }

        int sleep_ms = 0;
        size_t current_connections = 0;

        {
            std::lock_guard<std::mutex> lock(threads_mutex);
            current_connections = connection_threads.size();

            if (current_connections >= static_cast<size_t>(cfg.max_connections)) {
                std::cerr << "[WARN] Max connections reached, closing new connection\n";
                ::close(client_fd);
                continue;
            }

            if (current_connections + 4 >= static_cast<size_t>(cfg.max_connections)) {
                int cur  = g_accept_backpressure_ms.load(std::memory_order_relaxed);
                int next = std::min(cur + 5, 100);
                g_accept_backpressure_ms.store(next, std::memory_order_relaxed);
                sleep_ms = next;
            } else {
                int cur = g_accept_backpressure_ms.load(std::memory_order_relaxed);
                if (cur > 0) g_accept_backpressure_ms.store(cur - 1, std::memory_order_relaxed);
            }
        }

        if (cfg.verbose && sleep_ms > 0) {
            std::cerr << "[DEBUG] Backpressure sleep " << sleep_ms << "ms ("
                    << current_connections << "/" << cfg.max_connections << " active)\n";
        }

        if (sleep_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        }

        set_socket_timeouts(client_fd, cfg.socket_timeout_sec);
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        {
            std::lock_guard<std::mutex> lock(threads_mutex);

            if (connection_threads.size() >= static_cast<size_t>(cfg.max_connections)) {
                std::cerr << "[WARN] Connection limit reached after backpressure, dropping\n";
                struct linger lin{1, 0};
                setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
                ::close(client_fd);
                continue;
            }

            auto done = std::make_shared<std::atomic<bool>>(false);
            std::thread th([client_fd, cfg, done]() {
                handle_connection(client_fd, cfg);
                done->store(true, std::memory_order_release);
            });

            connection_threads.emplace_back(ConnThread{std::move(th), std::move(done)});
        }
    }

    ::close(listen_fd);
    
    if (cleanup_thread.joinable()) {
        cleanup_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(threads_mutex);
        for (auto& t : connection_threads) {
            if (t.th.joinable()) {
                t.th.join();
            }
        }
    }

    std::cerr << "[INFO] Proxy shutdown complete" << std::endl;
    return 0;
}
