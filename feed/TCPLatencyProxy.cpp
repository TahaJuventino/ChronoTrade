#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <iomanip>  // FIXED: Added missing header for std::fixed and std::setprecision
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <memory>

#include <climits>
#include <algorithm>
#include <netinet/in.h>
#include <cassert>

// Platform detection and compatibility layer
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// Feature detection for socket options
#ifdef __linux__
#define HAVE_TCP_KEEPIDLE 1
#define HAVE_TCP_KEEPINTVL 1
#define HAVE_TCP_KEEPCNT 1
#define HAVE_SO_DOMAIN 1
#define HAVE_ACCEPT4 1
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif
#ifndef SOCK_NONBLOCK  
#define SOCK_NONBLOCK 04000
#endif
#endif

#ifdef __APPLE__
#define HAVE_TCP_KEEPCNT 1
#define HAVE_TCP_KEEPALIVE 1
#define HAVE_SO_NOSIGPIPE 1
#endif

#ifndef SOMAXCONN
#define SOMAXCONN 128
#endif

using std::chrono::steady_clock;
using std::chrono::seconds;
using std::chrono::milliseconds;

// Global SIGPIPE suppression for Unix platforms
struct SigpipeGuard { 
    SigpipeGuard() { ::signal(SIGPIPE, SIG_IGN); } 
} _sigpipe_guard;

static constexpr int kPollMs = 100;
static constexpr int kMaxRetryDelay = 50;
static constexpr int kDrainTimeoutMs = 200;
static constexpr int kDrainPollStepMs = 50;
static constexpr size_t kMinQuantumBytes = 1024; // Minimum bandwidth quantum
static constexpr int kMinIdleTimeoutSec = 10;
static constexpr int kMaxIdleTimeoutSec = 3600;

enum class PipeResult { Ok, Eof, Error };

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
    int burst_seconds = 2;
    bool http_friendly_errors = false;
    bool rst_on_upstream_connect_fail = false;
    bool rst_on_midstream_errors = false;
    int socket_timeout_sec = 10;
    int idle_timeout_sec = 300;
    bool verbose = false;
    bool v6_only = false;
    uint32_t seed = 0;
    bool seed_auto_increment = true;
    int max_latency_ms = 2000;
};

// Forward declarations
struct ConnectionStats;
struct DirectionFlags;
class FdGuard;

static DirectionFlags parse_direction(const std::string& d);
static PipeResult proxy_pipe(int from_fd, int to_fd, const ProxyConfig& cfg, bool enabled,
                             std::atomic<bool>& stop_flag, const char* tag, ConnectionStats& stats);
static bool safe_send_all(int fd, const char* data, size_t len,
                          class PrecisionBandwidthThrottle* throttle = nullptr,
                          bool cap_small_chunks = false);

// Socket option setting with graceful degradation
inline void try_setsockopt(int fd, int level, int optname, const void* val, socklen_t len, const char* what, bool verbose = false) {
    if (::setsockopt(fd, level, optname, val, len) < 0) {
        if (errno == ENOPROTOOPT || errno == ENOTSUP) {
            if (verbose) {
                std::cerr << "[DEBUG] " << what << " not supported on this platform" << std::endl;
            }
        } else {
            std::cerr << "[WARN] " << what << " failed: " << strerror(errno) << std::endl;
        }
    }
}

// RAII file descriptor guard
class FdGuard {
    public:
        explicit FdGuard(int fd = -1) : fd_(fd) {}
        ~FdGuard() { if (fd_ >= 0) ::close(fd_); }
        
        FdGuard(const FdGuard&) = delete;
        FdGuard& operator=(const FdGuard&) = delete;
        
        FdGuard(FdGuard&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
        FdGuard& operator=(FdGuard&& other) noexcept {
            if (this != &other) {
                if (fd_ >= 0) ::close(fd_);
                fd_ = other.fd_;
                other.fd_ = -1;
            }
            return *this;
        }
        
        int get() const { return fd_; }
        int release() { int tmp = fd_; fd_ = -1; return tmp; }
        void reset(int new_fd = -1) { 
            if (fd_ >= 0) ::close(fd_); 
            fd_ = new_fd; 
        }
        
        explicit operator bool() const { return fd_ >= 0; }

    private:
        int fd_;
};

// Enhanced precision bandwidth throttle with quantum guarantee
class PrecisionBandwidthThrottle {
    int64_t kbps_;
    bool enable_burst_;
    std::mutex mu_;
    std::chrono::steady_clock::time_point last_;
    int64_t tokens_;          // bytes
    int64_t frac_acc_;        // numerator accumulator (bytes * 1e6)
    int64_t max_tokens_;      // bytes
    int64_t num_bytes_per_s_; // = kbps * 1000 / 8
    bool verbose_;
    bool logged_cap_warning_ = false;

    public:
        PrecisionBandwidthThrottle(int kbps, bool burst, int burst_seconds = 2, bool verbose = false)
        : kbps_(kbps)
        , enable_burst_(burst)
        , last_(std::chrono::steady_clock::now())
        , tokens_(0)
        , frac_acc_(0)
        , verbose_(verbose) {
            
            // Clamp kbps to prevent overflow (max ~1Gbps) with warning
            constexpr int64_t kMaxGbpsKbps = static_cast<int64_t>(1000000); // 1 Gbps in kbps
            int64_t safe_kbps = std::min<int64_t>(static_cast<int64_t>(kbps), kMaxGbpsKbps);
            if (safe_kbps < kbps && !logged_cap_warning_) {
                std::cerr << "[WARN] Bandwidth capped from " << kbps << " to " << safe_kbps << " kbps (1Gbps max)" << std::endl;
                logged_cap_warning_ = true;
            }
            
            num_bytes_per_s_ = safe_kbps * 1000 / 8;
            max_tokens_ = num_bytes_per_s_ * std::max(1, burst ? burst_seconds : 1);
            
            // Ensure minimum quantum for progress guarantee
            if (num_bytes_per_s_ > 0) {
                max_tokens_ = std::max(max_tokens_, static_cast<int64_t>(kMinQuantumBytes));
            }
            
            if (burst) tokens_ = max_tokens_;
            
            if (verbose) {
                std::cerr << "[DEBUG] PrecisionBandwidthThrottle: " << safe_kbps << " kbps = " 
                        << num_bytes_per_s_ << " bytes/sec, max_tokens=" << max_tokens_ 
                        << ", burst=" << burst << " (" << (max_tokens_ / std::max<int64_t>(1, num_bytes_per_s_)) 
                        << "s)" << std::endl;
            }
        }

        void throttle(size_t bytes) {
            if (kbps_ <= 0 || bytes == 0) return;
            int64_t need = static_cast<int64_t>(bytes);

            for (;;) {
                int64_t deficit;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    auto now = std::chrono::steady_clock::now();
                    auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_).count();
                    if (dt_us > 0) {
                        add_tokens(dt_us);
                        last_ = now;
                    }
                    deficit = need - tokens_;
                    if (deficit <= 0) { tokens_ -= need; return; }
                }
                int64_t us = calculate_sleep_us(deficit);
                std::this_thread::sleep_for(std::chrono::microseconds(us));
            }
        }

        size_t allowance(size_t max_bytes = 8192) {
            if (kbps_ <= 0) return max_bytes;
            std::lock_guard<std::mutex> lk(mu_);
            auto now = std::chrono::steady_clock::now();
            auto dt_us = std::chrono::duration_cast<std::chrono::microseconds>(now - last_).count();
            if (dt_us > 0) {
                add_tokens(dt_us);
                last_ = now;
            }
            int64_t avail = std::max<int64_t>(0, tokens_);
            // Guarantee minimum quantum for progress - IMPROVED: Cap by max_bytes and scale for low kbps
            if (avail == 0 && num_bytes_per_s_ > 0) {
                avail = std::min<int64_t>(kMinQuantumBytes, max_tokens_ / 4);
                avail = std::min<int64_t>(avail, static_cast<int64_t>(max_bytes));
            }
            return static_cast<size_t>(std::min<int64_t>(avail, static_cast<int64_t>(max_bytes)));
        }

        int64_t next_refill_delay_us(int64_t deficit_bytes = 1) const {
            if (num_bytes_per_s_ <= 0) return 2000; // 2ms fallback
            return std::max<int64_t>(2000,  // minimum 2ms for better scheduling
                std::min<int64_t>(50000,    // maximum 50ms
                    (deficit_bytes * 1000000) / num_bytes_per_s_));
        }

    private:
        void add_tokens(int64_t dt_us) {
            // Safe overflow handling - split into seconds and microseconds
            int64_t sec = dt_us / 1000000;
            int64_t usec = dt_us % 1000000;
            
            // Whole seconds are always safe
            int64_t add_bytes = sec * num_bytes_per_s_;
            
            // For fractional part, use safe multiplication
            if (num_bytes_per_s_ <= LLONG_MAX / 1000000) {
                // Direct multiplication is safe
                int64_t add_frac = num_bytes_per_s_ * usec;
                add_bytes += (frac_acc_ + add_frac) / 1000000;
                frac_acc_ = (frac_acc_ + add_frac) % 1000000;
            } else {
                // Split to avoid overflow
                int64_t bytes_per_usec = num_bytes_per_s_ / 1000000;
                int64_t remainder = num_bytes_per_s_ % 1000000;
                add_bytes += bytes_per_usec * usec;
                
                int64_t add_frac = remainder * usec;
                add_bytes += (frac_acc_ + add_frac) / 1000000;
                frac_acc_ = (frac_acc_ + add_frac) % 1000000;
            }
            
            tokens_ = std::min(max_tokens_, tokens_ + add_bytes);
        }

        int64_t calculate_sleep_us(int64_t deficit) const {
            return std::max<int64_t>(2000, std::min<int64_t>(50000,
                            (deficit * 1000000) / std::max<int64_t>(1, num_bytes_per_s_)));
        }
};

static std::atomic<int> g_accept_backpressure_ms{0};

struct ConnThread {
    std::unique_ptr<std::thread> th;
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
    std::chrono::steady_clock::time_point last_activity;
    std::chrono::steady_clock::time_point start_time;
    mutable std::mutex activity_mutex;
    
    ConnectionStats() : start_time(std::chrono::steady_clock::now()) {
        update_activity();
    }
    
    void update_activity() {
        std::lock_guard<std::mutex> lock(activity_mutex);
        last_activity = std::chrono::steady_clock::now();
    }
    
    std::chrono::steady_clock::time_point get_last_activity() const {
        std::lock_guard<std::mutex> lock(activity_mutex);
        return last_activity;
    }
};

struct DirectionFlags {
    bool up = true;
    bool down = true;
    
    bool both_disabled() const { return !up && !down; }
};

void sigint_handler(int) {
    g_running = false;
}

// Error categorization for better diagnostics
static const char* categorize_errno(int err) {
    switch (err) {
        case ECONNRESET: return "connection reset by peer";
        case ETIMEDOUT: return "network timeout";
        case EPIPE: return "write on closed socket";
        case ECONNREFUSED: return "connection refused";
        case EHOSTUNREACH: return "host unreachable";
        case ENETUNREACH: return "network unreachable";
        case EADDRINUSE: return "address already in use";
        case EADDRNOTAVAIL: return "address not available";
        case EMFILE: case ENFILE: return "file descriptor limit reached";
        case ENOBUFS: case ENOMEM: return "insufficient memory/buffers";
        default: return strerror(err);
    }
}

static void usage(const char* argv0) {
    std::cerr <<
        "Usage: " << argv0 << " \\\n"
        "  --listen-host 0.0.0.0 --listen-port 9000 \\\n"
        "  --upstream-host 127.0.0.1 --upstream-port 7000 \\\n"
        "  [--latency-ms 50] [--jitter-ms 10] [--drop-rate 0.05] [--dup-rate 0.01] \\\n"
        "  [--bandwidth-kbps 256] [--buffer-bytes 8192] [--direction up|down|both] \\\n"
        "  [--max-connections 128] [--no-half-close] [--half-close] \\\n"
        "  [--enable-burst] [--burst-seconds 2] [--max-latency-ms 2000] \\\n"
        "  [--http-friendly-errors] [--rst-on-upstream-connect-fail] \\\n"
        "  [--rst-on-midstream-errors] [--socket-timeout-sec 10] \\\n"
        "  [--idle-timeout-sec 300] [--v6-only] [--verbose] [--seed 1234]\n"
        "\n"
        "Limits: bandwidth-kbps <= 1000000, buffer-bytes <= 1MB, max-connections <= 100000\n"
        "        idle-timeout-sec: 10-3600 seconds\n"
        "Note: --v6-only disables IPv4-mapped addresses; IPv4 clients will fail to connect.\n";
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

// Round buffer size to nearest lower power of two
static size_t round_to_power_of_two(size_t n) {
    if (n == 0) return 1024; // minimum
    size_t result = 1;
    while (result <= n && result <= (1ULL << 20)) { // max 1MB
        if (result == n) return result; // exact match
        result <<= 1;
    }
    return result >> 1; // nearest lower
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
        else if (a=="--burst-seconds") { if (!next(cfg.burst_seconds)) return false; }
        else if (a=="--enable-burst"){ cfg.enable_burst = true; }
        else if (a=="--max-latency-ms") { if (!next(cfg.max_latency_ms)) return false; }
        else if (a=="--http-friendly-errors"){ cfg.http_friendly_errors = true; }
        else if (a=="--rst-on-upstream-connect-fail"){ cfg.rst_on_upstream_connect_fail = true; }
        else if (a=="--rst-on-midstream-errors"){ cfg.rst_on_midstream_errors = true; }
        else if (a=="--socket-timeout-sec"){ if (!next(cfg.socket_timeout_sec)) return false; }
        else if (a=="--idle-timeout-sec"){ if (!next(cfg.idle_timeout_sec)) return false; }
        else if (a=="--v6-only")     { cfg.v6_only = true; }
        else if (a=="--verbose" || a=="-v"){ cfg.verbose = true; }
        else if (a=="--seed")        { int s; if (!next(s)) return false; cfg.seed = static_cast<uint32_t>(s); }
        else { usage(argv[0]); return false; }
    }
    
    // Input validation and clamping
    cfg.drop_rate = std::max(0.0, std::min(1.0, cfg.drop_rate));
    cfg.dup_rate = std::max(0.0, std::min(1.0, cfg.dup_rate));
    cfg.jitter_ms = std::max(0, cfg.jitter_ms);
    cfg.latency_ms = std::max(0, cfg.latency_ms);
    cfg.bandwidth_kbps = std::max(0, std::min(1000000, cfg.bandwidth_kbps)); // Max 1Gbps
    cfg.socket_timeout_sec = std::max(1, std::min(300, cfg.socket_timeout_sec));
    cfg.idle_timeout_sec = std::max(kMinIdleTimeoutSec, std::min(kMaxIdleTimeoutSec, cfg.idle_timeout_sec));
    cfg.burst_seconds = std::max(1, std::min(10, cfg.burst_seconds));
    cfg.max_latency_ms = std::max(100, std::min(60000, cfg.max_latency_ms));
    cfg.max_connections = std::max(1, std::min(100000, cfg.max_connections));
    
    // Round buffer size to power of two
    size_t original_buffer = cfg.buffer_bytes;
    cfg.buffer_bytes = std::max(size_t(1024), std::min(size_t(1<<20), cfg.buffer_bytes)); // 1KB to 1MB
    cfg.buffer_bytes = round_to_power_of_two(cfg.buffer_bytes);
    if (original_buffer != cfg.buffer_bytes) {
        std::cerr << "[INFO] Rounded buffer_bytes from " << original_buffer 
                  << " to " << cfg.buffer_bytes << std::endl;
    }
    
    // Validate combined drop/dup rates don't starve traffic
    if (cfg.drop_rate + cfg.dup_rate > 0.9) {
        std::cerr << "[WARN] Combined drop+dup rates > 90% may starve traffic" << std::endl;
    }
    
    // IPv6-only validation
    if (cfg.v6_only) {
        // Check if listen_host is an IPv4 literal
        struct sockaddr_in ipv4_test;
        if (inet_pton(AF_INET, cfg.listen_host.c_str(), &ipv4_test.sin_addr) == 1) {
            std::cerr << "[ERROR] --v6-only specified but listen_host is IPv4 literal: " << cfg.listen_host << std::endl;
            return false;
        }
    }
    
    return true;
}

static DirectionFlags parse_direction(const std::string& d) {
    DirectionFlags f{};
    if (d=="up")   { f.down = false; }
    else if (d=="down") { f.up = false; }
    else if (d=="both") { /* default */ }
    else { throw std::invalid_argument("Invalid --direction (expected up|down|both)"); }
    
    if (f.both_disabled()) {
        throw std::invalid_argument("Cannot disable both directions - proxy would be useless");
    }
    
    return f;
}

// HTTP detection over the first TCP fragment - best effort with timeout protection
static bool looks_like_http(const char* data, size_t len) {
    if (!data || len < 4) return false;
    
    // Skip leading whitespace
    size_t i = 0;
    while (i < len && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n')) {
        ++i;
    }
    
    if (len - i < 4) return false;
    
    // Case-insensitive method check
    auto matches = [&](const char* method, size_t method_len) {
        if (len - i < method_len) return false;
        for (size_t k = 0; k < method_len; ++k) {
            char c = data[i + k];
            if (c >= 'a' && c <= 'z') c -= 32; // toupper
            if (c != method[k]) return false;
        }
        return true;
    };
    
    return matches("GET ", 4) || matches("POST ", 5) || matches("PUT ", 4) || 
           matches("HEAD ", 5) || matches("DELETE ", 7) || matches("PATCH ", 6) ||
           matches("OPTIONS ", 8) || matches("CONNECT ", 8) || matches("TRACE ", 6) ||
           matches("HTTP/", 5);
}

// FIXED: HTTP helper responses with proper static_assert
static constexpr size_t k503BodyLen = sizeof("Upstream Unavailable") - 1;
static void send_503(int fd) {
    static const char k503[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: 20\r\n\r\n"
        "Upstream Unavailable";
    static_assert(k503BodyLen == 20, "503 body length mismatch");
    (void)safe_send_all(fd, k503, sizeof(k503) - 1, nullptr, false);
}

static constexpr size_t k429BodyLen = sizeof("Rate limit hit.\n") - 1;
static void send_429(int fd) {
    static const char k429[] =
        "HTTP/1.1 429 Too Many Requests\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Connection: close\r\n"
        "Content-Length: 16\r\n\r\n"
        "Rate limit hit.\n";
    static_assert(k429BodyLen == 16, "429 body length mismatch");
    (void)safe_send_all(fd, k429, sizeof(k429) - 1, nullptr, false);
}

// Enhanced cross-platform socket options with graceful degradation
static void set_socket_options(int fd, bool verbose = false) {
    int one = 1;
    try_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one), "TCP_NODELAY", verbose);
    try_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one), "SO_KEEPALIVE", verbose);

    // Platform-specific TCP keepalive tuning - graceful degradation
#ifdef HAVE_TCP_KEEPIDLE
    int idle = 30;
    try_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle), "TCP_KEEPIDLE", verbose);
#endif

#ifdef HAVE_TCP_KEEPINTVL
    int intvl = 10;
    try_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl), "TCP_KEEPINTVL", verbose);
#endif

#ifdef HAVE_TCP_KEEPCNT
    int cnt = 5;
    try_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt), "TCP_KEEPCNT", verbose);
#endif

#ifdef HAVE_TCP_KEEPALIVE
    int intvl_mac = 10;
    try_setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &intvl_mac, sizeof(intvl_mac), "TCP_KEEPALIVE", verbose);
#endif

#ifdef HAVE_SO_NOSIGPIPE
    int nosigpipe = 1;
    try_setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe), "SO_NOSIGPIPE", verbose);
#endif

    // CLOEXEC everywhere
    int fd_flags = fcntl(fd, F_GETFD);
    if (fd_flags >= 0) {
        fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
    }
}

// Non-blocking connect with timeout and improved error handling
static int dial(const std::string& host, int port, int timeout_ms = 0) {
    addrinfo hints{}, *res=nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_s = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
    if (rc != 0) { 
        std::cerr << "[ERROR] getaddrinfo(" << host << ":" << port << "): " << gai_strerror(rc) << std::endl; 
        return -1; 
    }

    int fd = -1;
    for (auto p = res; p; p = p->ai_next) {
        // FIXED: Removed SOCK_CLOEXEC from socket() for better portability
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd >= 0) {
            int fd_flags = fcntl(fd, F_GETFD);
            if (fd_flags >= 0) fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
        }
        if (fd < 0) continue;

        set_socket_options(fd);
        
        // Set nonblocking for connect
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;

        if (errno == EINPROGRESS) {
            struct pollfd pfd{fd, POLLOUT, 0};
            int to = timeout_ms > 0 ? timeout_ms : 1000; // fall back 1s
            int pr = poll(&pfd, 1, to);
            if (pr == 1 && (pfd.revents & (POLLERR|POLLHUP|POLLNVAL))) { 
                ::close(fd); 
                fd = -1; 
                continue; 
            }
            if (pr == 1 && (pfd.revents & POLLOUT)) {
                // Check SO_ERROR after POLLOUT to ensure connection succeeded
                int err = 0; 
                socklen_t len = sizeof(err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    break;
                }
                // Connection failed
                std::cerr << "[DEBUG] connect SO_ERROR: " << categorize_errno(err) << std::endl;
            }
            // IMPROVED: Log timeout when verbose mode would be helpful
            if (pr == 0) {
                std::cerr << "[DEBUG] connect timeout to " << host << ":" << port << std::endl;
            }
        } else {
            std::cerr << "[DEBUG] connect immediate failure: " << categorize_errno(errno) << std::endl;
        }
        ::close(fd); 
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        std::cerr << "[ERROR] connect to " << host << ":" << port << " failed" << std::endl;
    }
    return fd;
}

// Enhanced listen_on with IPv6-first preference and explicit error logging
int listen_on(const std::string& host, int port, int backlog, bool v6_only = false) {
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

    // Collect candidates, prefer IPv6 when not v6_only to attempt dual-stack
    std::vector<addrinfo*> v6_candidates, v4_candidates;
    for (auto p = res; p; p = p->ai_next) {
        if (p->ai_family == AF_INET6) {
            v6_candidates.push_back(p);
        } else if (p->ai_family == AF_INET) {
            v4_candidates.push_back(p);
        }
    }

    auto try_bind = [&](addrinfo* p) -> int {
        const char* family_str = (p->ai_family == AF_INET6) ? "IPv6" : "IPv4";
        
        // FIXED: Consistent socket creation without conditional SOCK_CLOEXEC
        int fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd >= 0) {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            int fd_flags = fcntl(fd, F_GETFD);
            if (fd_flags >= 0) fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC);
        }
        if (fd < 0) {
            std::cerr << "[ERROR] socket(" << family_str << "): " << categorize_errno(errno) << std::endl;
            return -1;
        }
        
        int yes = 1; 
        try_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes), "SO_REUSEADDR");
        
        if (p->ai_family == AF_INET6) {
            int v6only_flag = v6_only ? 1 : 0;
            try_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only_flag, sizeof(v6only_flag), "IPV6_V6ONLY");
        }
        
        if (::bind(fd, p->ai_addr, p->ai_addrlen) != 0) {
            int bind_err = errno;
            std::cerr << "[ERROR] bind(" << family_str << ", port " << port << "): " 
                      << categorize_errno(bind_err) << std::endl;
            ::close(fd);
            return -1;
        }
        
        if (::listen(fd, backlog) != 0) {
            int listen_err = errno;
            std::cerr << "[ERROR] listen(" << family_str << ", backlog " << backlog << "): " 
                      << categorize_errno(listen_err) << std::endl;
            ::close(fd);
            return -1;
        }
        
        std::cerr << "[INFO] Successfully bound to " << family_str << " address" << std::endl;
        return fd;
    };

    int fd = -1;
    
    // Try IPv6 first for dual-stack potential
    for (auto* p : v6_candidates) {
        if ((fd = try_bind(p)) >= 0) break;
    }
    
    // Fall back to IPv4 if IPv6 failed
    if (fd < 0) {
        for (auto* p : v4_candidates) {
            if ((fd = try_bind(p)) >= 0) break;
        }
    }

    freeaddrinfo(res);
    
    if (fd >= 0) {
        // Log the actual socket family that was bound
#ifdef HAVE_SO_DOMAIN
        int fam = 0; 
        socklen_t fl = sizeof(fam);
        if (getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &fam, &fl) == 0) {
            std::cerr << "[INFO] Listening family: " << (fam == AF_INET6 ? "IPv6" : fam == AF_INET ? "IPv4" : "Other");
            if (fam == AF_INET6 && !v6_only) {
                std::cerr << " (dual-stack enabled)";
            }
            std::cerr << std::endl;
        }
#endif
    }
    
    return fd;
}

static void sleep_with_latency(std::mt19937& rng, int base_ms, int jitter_ms, int max_ms) {
    int jitter = 0;
    if (jitter_ms > 0) {
        std::uniform_int_distribution<int> dist(-jitter_ms, jitter_ms);
        jitter = dist(rng);
    }
    int delay = std::max(0, base_ms + jitter);
    delay = std::min(delay, max_ms);
    if (delay > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    }
}

// Enhanced send result for better error handling
enum class SendResult { Success, WouldBlock, Error, Closed };

// Safe send with POLLOUT wait on EAGAIN - enhanced with explicit POLLOUT check and SO_ERROR validation
static SendResult safe_send_all_detailed(int fd, const char* data, size_t len,
                                        PrecisionBandwidthThrottle* throttle,
                                        bool cap_small_chunks) {
    if (len == 0) return SendResult::Success;

    size_t sent = 0;
    while (sent < len) {
        size_t remaining = len - sent;
        size_t slice = remaining;
        
        // Apply throttling
        if (throttle) {
            size_t allowed = throttle->allowance(remaining);
            if (allowed == 0) {
                int64_t deficit_bytes = std::min<int64_t>(remaining, 1024);
                int64_t sleep_us = throttle->next_refill_delay_us(deficit_bytes);
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
                continue;
            }
            slice = std::min(slice, allowed);
        }
        
        // Cap chunks when doing impairment simulation
        if (cap_small_chunks) {
            slice = std::min(slice, size_t(512));
        }
        
        // Ensure slice fits in int for send()
        slice = std::min(slice, size_t(INT_MAX));

        ssize_t n = ::send(fd, data + sent, static_cast<int>(slice), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd{fd, POLLOUT, 0};
                int pr = poll(&pfd, 1, kPollMs);
                if (pr <= 0) return SendResult::WouldBlock; // FIXED: caller will retry rather than abort
                if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return SendResult::Closed;
                if (!(pfd.revents & POLLOUT)) continue; // avoid busy loop if only POLLIN
                
                // Check SO_ERROR after POLLOUT to ensure socket is still valid
                int sock_err = 0;
                socklen_t err_len = sizeof(sock_err);
                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &err_len) == 0 && sock_err != 0) {
                    return (sock_err == ECONNRESET || sock_err == EPIPE) ? SendResult::Closed : SendResult::Error;
                }
                continue;
            }
            return (errno == ECONNRESET || errno == EPIPE) ? SendResult::Closed : SendResult::Error;
        }

        // Consume tokens after successful send
        if (throttle && n > 0) {
            throttle->throttle(static_cast<size_t>(n));
        }
        sent += static_cast<size_t>(n);
    }
    return SendResult::Success;
}

// Compatibility wrapper
static bool safe_send_all(int fd, const char* data, size_t len,
                          PrecisionBandwidthThrottle* throttle,
                          bool cap_small_chunks) {
    SendResult result = safe_send_all_detailed(fd, data, len, throttle, cap_small_chunks);
    return result == SendResult::Success;
}

// Simple data chunker for impairment simulation
static bool process_with_impairments(const char* data, size_t len, 
                                   std::mt19937& rng, const ProxyConfig& cfg,
                                   int to_fd, PrecisionBandwidthThrottle* throttle,
                                   ConnectionStats& stats) {
    std::uniform_real_distribution<double> U(0.0, 1.0);
    
    // Process in ~MTU chunks
    const size_t CHUNK_SIZE = 1400;
    size_t pos = 0;
    
    while (pos < len) {
        size_t chunk_size = std::min(CHUNK_SIZE, len - pos);
        const char* chunk_data = data + pos;
        
        // Drop simulation
        if (cfg.drop_rate > 0.0 && U(rng) < cfg.drop_rate) {
            stats.packets_dropped.fetch_add(1, std::memory_order_relaxed);
            if (cfg.latency_ms > 0 || cfg.jitter_ms > 0) {
                sleep_with_latency(rng, cfg.latency_ms, cfg.jitter_ms, cfg.max_latency_ms);
            }
            pos += chunk_size;
            continue;
        }

        // Latency + jitter
        if (cfg.latency_ms > 0 || cfg.jitter_ms > 0) {
            sleep_with_latency(rng, cfg.latency_ms, cfg.jitter_ms, cfg.max_latency_ms);
        }

        // Send original
        SendResult result = safe_send_all_detailed(to_fd, chunk_data, chunk_size, throttle, true);
        if (result != SendResult::Success) {
            return false;
        }
        stats.bytes_sent.fetch_add(chunk_size, std::memory_order_relaxed);

        // Possibly duplicate
        if (cfg.dup_rate > 0.0 && U(rng) < cfg.dup_rate) {
            stats.packets_duplicated.fetch_add(1, std::memory_order_relaxed);
            // Add small jitter for duplicate
            if (cfg.jitter_ms > 0) {
                std::uniform_int_distribution<int> dup_jitter(1, std::max(1, cfg.jitter_ms / 2));
                std::this_thread::sleep_for(std::chrono::milliseconds(dup_jitter(rng)));
            }
            result = safe_send_all_detailed(to_fd, chunk_data, chunk_size, throttle, true);
            if (result != SendResult::Success) {
                return false;
            }
            stats.bytes_sent.fetch_add(chunk_size, std::memory_order_relaxed);
        }
        
        pos += chunk_size;
    }
    return true;
}

// Enhanced RST vs graceful close handling with half_close policy
static void rst_close(int fd) {
    if (fd < 0) return;
    linger lin{1, 0}; // 0s linger => RST (may truncate in-flight data)
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    ::close(fd);
}

static void graceful_half_close(int fd) {
    if (fd >= 0) ::shutdown(fd, SHUT_WR); // let peer read what we sent
}

static void finish_pair(int& a, int& b, PipeResult ra, PipeResult rb, bool rst_on_error, bool half_close) {
    if (a < 0 && b < 0) return; // already closed
    
    const bool error = (ra == PipeResult::Error) || (rb == PipeResult::Error);
    if (error && rst_on_error) { 
        if (a >= 0) { rst_close(a); a = -1; }
        if (b >= 0) { rst_close(b); b = -1; }
        return; 
    }
    
    if (half_close) {
        if (a >= 0) ::shutdown(a, SHUT_WR);
        if (b >= 0) ::shutdown(b, SHUT_WR);
    }
    
    if (a >= 0) { ::close(a); a = -1; }
    if (b >= 0) { ::close(b); b = -1; }
}

// Proxy pipe with proper error handling and status return
static PipeResult proxy_pipe(int from_fd, int to_fd, const ProxyConfig& cfg, bool enabled,
                             std::atomic<bool>& stop_flag, const char* tag, 
                             ConnectionStats& stats) {

    const size_t safe_buffer_size = std::min(cfg.buffer_bytes, static_cast<size_t>(INT_MAX));
    std::vector<char> buf(safe_buffer_size);

    // Enhanced seed generation for reproducible testing - IMPROVED: Add timestamp for uniqueness
    uint32_t connection_seed = cfg.seed;
    if (cfg.seed != 0 && cfg.seed_auto_increment) {
        auto now = std::chrono::steady_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        connection_seed = cfg.seed ^ static_cast<uint32_t>(from_fd) ^ static_cast<uint32_t>(to_fd) ^ static_cast<uint32_t>(timestamp);
    }
    std::mt19937 rng(connection_seed ? connection_seed : std::random_device{}());

    std::unique_ptr<PrecisionBandwidthThrottle> throttle;
    if (enabled && cfg.bandwidth_kbps > 0) {
        throttle = std::make_unique<PrecisionBandwidthThrottle>(
            cfg.bandwidth_kbps, cfg.enable_burst, cfg.burst_seconds, cfg.verbose);
    }

    bool need_impairments = enabled && (cfg.latency_ms > 0 || cfg.drop_rate > 0.0 || cfg.dup_rate > 0.0);

    while (g_running && !stop_flag.load()) {
        // Use poll to check readability with proper error handling
        struct pollfd pfd = {from_fd, POLLIN, 0};
        int poll_result = poll(&pfd, 1, kPollMs);
        
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[DEBUG] " << tag << " poll error: " << categorize_errno(errno) << std::endl;
            return PipeResult::Error;
        }
        if (poll_result == 0) continue; // timeout
        
        // Handle poll events properly
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            if (cfg.verbose) {
                std::cerr << "[DEBUG] " << tag << " poll events: " 
                          << (pfd.revents & POLLERR ? "ERR " : "")
                          << (pfd.revents & POLLHUP ? "HUP " : "")
                          << (pfd.revents & POLLNVAL ? "NVAL " : "") << std::endl;
            }
            return PipeResult::Eof;
        }
        if (!(pfd.revents & POLLIN)) continue;

        ssize_t n = ::recv(from_fd, buf.data(), safe_buffer_size, 0);

        if (n == 0) {
            if (cfg.verbose) std::cerr << "[DEBUG] " << tag << " EOF" << std::endl;
            return PipeResult::Eof;
        }

        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            std::cerr << "[DEBUG] " << tag << " recv error: " << categorize_errno(errno) << std::endl;
            return PipeResult::Error;
        }

        stats.bytes_received.fetch_add(static_cast<size_t>(n), std::memory_order_relaxed);
        stats.update_activity();

        // Fast path: no impairments except throttling
        if (!enabled || !need_impairments) {
            // FIXED: Handle EAGAIN back-pressure properly by retrying WouldBlock
            SendResult result;
            do {
                result = safe_send_all_detailed(to_fd, buf.data(), static_cast<size_t>(n), throttle.get(), false);
                if (result == SendResult::WouldBlock) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            } while (result == SendResult::WouldBlock);
            if (result == SendResult::Closed) return PipeResult::Eof;
            if (result != SendResult::Success) return PipeResult::Error;
            stats.bytes_sent.fetch_add(static_cast<size_t>(n), std::memory_order_relaxed);
            stats.update_activity();
            continue;
        }

        // Impairments path: process with drops/dups/latency
        if (!process_with_impairments(buf.data(), static_cast<size_t>(n), rng, cfg, 
                                    to_fd, throttle.get(), stats)) {
            return PipeResult::Error;
        }
        stats.update_activity();
    }
    
    return PipeResult::Ok;
}

// Enhanced drain function using poll instead of SO_RCVTIMEO
static void drain_with_poll(int fd, int timeout_ms = kDrainTimeoutMs) {
    if (fd < 0) return;
    
    char tmp[1024];
    auto start = std::chrono::steady_clock::now();
    
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
           std::chrono::steady_clock::now() - start).count() < timeout_ms) {
        
        struct pollfd pfd{fd, POLLIN, 0};
        int poll_result = poll(&pfd, 1, kDrainPollStepMs);
        
        if (poll_result <= 0) break; // timeout or error
        if (!(pfd.revents & POLLIN)) break;
        
        if (recv(fd, tmp, sizeof(tmp), MSG_DONTWAIT) <= 0) break;
    }
}

// HTTP friendly peek with timeout protection
static bool try_http_peek(int fd, char* buf, size_t buf_size, ssize_t& bytes_peeked) {
    bytes_peeked = 0;
    
    // Non-blocking peek to avoid stalling
    bytes_peeked = recv(fd, buf, buf_size - 1, MSG_PEEK | MSG_DONTWAIT);
    if (bytes_peeked > 0) {
        buf[bytes_peeked] = '\0';
        return true;
    }
    
    // IMPROVED: Early return if peer closed
    if (bytes_peeked == 0) return false;
    
    if (bytes_peeked < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        // Try once with brief wait
        struct pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 50) == 1 && (pfd.revents & POLLIN)) { // 50ms max
            bytes_peeked = recv(fd, buf, buf_size - 1, MSG_PEEK | MSG_DONTWAIT);
            if (bytes_peeked > 0) {
                buf[bytes_peeked] = '\0';
                return true;
            }
        }
    }
    
    return false;
}

class LogRateLimiter {
    std::chrono::steady_clock::time_point last_log;
    std::chrono::milliseconds min_interval;
    std::mutex mu;
    
    public:
        LogRateLimiter(std::chrono::milliseconds interval = std::chrono::milliseconds(1000)) 
            : last_log(std::chrono::steady_clock::time_point::min()), min_interval(interval) {}
        
        bool should_log() {
            std::lock_guard<std::mutex> lock(mu);
            auto now = std::chrono::steady_clock::now();
            if (now - last_log >= min_interval) {
                last_log = now;
                return true;
            }
            return false;
        }
};

void handle_connection(int client_fd, const ProxyConfig& cfg) {
    FdGuard client_guard(client_fd);
    FdGuard upstream_guard;
    
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

    // Use timeout from config for dial
    int upstream_fd = dial(cfg.upstream_host, cfg.upstream_port, cfg.socket_timeout_sec * 1000);

    if (upstream_fd < 0) {
        std::cerr << "[ERROR] Failed to connect to upstream from " << client_ip << std::endl;

        // HTTP-friendly error handling with timeout protection
        if (cfg.http_friendly_errors) {
            char peek_buf[128] = {};
            ssize_t peeked = 0;
            if (try_http_peek(client_fd, peek_buf, sizeof(peek_buf), peeked)) {
                if (peeked > 0 && looks_like_http(peek_buf, static_cast<size_t>(peeked))) {
                    send_503(client_fd);
                    ::shutdown(client_fd, SHUT_WR);
                    
                    // Brief drain using poll
                    drain_with_poll(client_fd, 200);
                }
            }
        }

        // Use RST for upstream connect failures when specifically enabled
        if (cfg.rst_on_upstream_connect_fail) {
            linger lin{1, 0};
            setsockopt(client_fd, SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
        } else {
            graceful_half_close(client_fd);
        }
        return;
    }

    upstream_guard.reset(upstream_fd);

    DirectionFlags dirs = parse_direction(cfg.direction);
    std::atomic<bool> stop{false};
    
    ConnectionStats up_stats, down_stats;
    PipeResult up_result = PipeResult::Ok;
    PipeResult down_result = PipeResult::Ok;

    // Only spawn threads for enabled directions
    std::unique_ptr<std::thread> t_up, t_down;
    
    if (dirs.up) {
        t_up = std::make_unique<std::thread>([&](){
            up_result = proxy_pipe(client_fd, upstream_fd, cfg, true, stop, "UP", up_stats);
            stop = true;
        });
    } else {
        // Immediately close upstream write if up direction disabled
        ::shutdown(upstream_fd, SHUT_WR);
    }
    
    if (dirs.down) {
        t_down = std::make_unique<std::thread>([&](){
            down_result = proxy_pipe(upstream_fd, client_fd, cfg, true, stop, "DOWN", down_stats);
            stop = true;
        });
    } else {
        // Immediately close client write if down direction disabled
        ::shutdown(client_fd, SHUT_WR);
    }

    const auto THREAD_TIMEOUT = seconds(cfg.idle_timeout_sec);

    // Monitor for timeout or completion
    while (!stop.load() && g_running.load()) {
        auto now = steady_clock::now();
        
        auto up_last = up_stats.get_last_activity();
        auto down_last = down_stats.get_last_activity();
        auto most_recent = std::max(up_last, down_last);
        
        if (now - most_recent > THREAD_TIMEOUT) {
            std::cerr << "[WARN] Connection from " << client_ip 
                      << " idle timeout (" << cfg.idle_timeout_sec << "s), forcing close" << std::endl;
            stop = true;
            break;
        }
        std::this_thread::sleep_for(milliseconds(100));
    }

    // Graceful teardown sequence - set stop flag, join threads, then apply shutdown policy
    stop = true;
    
    if (t_up && t_up->joinable()) {
        t_up->join();
    }
    if (t_down && t_down->joinable()) {
        t_down->join();
    }

    // Apply proper shutdown policy based on results and config
    int client_fd_raw = client_guard.release();
    int upstream_fd_raw = upstream_guard.release();
    finish_pair(upstream_fd_raw, client_fd_raw, up_result, down_result, cfg.rst_on_midstream_errors, cfg.half_close);

    // Print stats with overflow safety
    auto total_elapsed = steady_clock::now() - up_stats.start_time;
    auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(total_elapsed).count();
    if (total_seconds > 0 && cfg.verbose) {
        uint64_t total_sent = up_stats.bytes_sent.load() + down_stats.bytes_sent.load();
        uint64_t total_received = up_stats.bytes_received.load() + down_stats.bytes_received.load();
        uint64_t total_dropped = up_stats.packets_dropped.load() + down_stats.packets_dropped.load();
        uint64_t total_duped = up_stats.packets_duplicated.load() + down_stats.packets_duplicated.load();
        double avg_throughput_kbps = (total_sent * 8.0) / (static_cast<double>(total_seconds) * 1000.0);
        
        std::cerr << "[INFO] Connection from " << client_ip << " summary: " 
                  << total_sent << " bytes sent, " << total_received << " bytes received, " 
                  << std::fixed << std::setprecision(1) << avg_throughput_kbps << " kbps avg, "
                  << total_dropped << " dropped, " << total_duped << " duplicated" << std::endl;
    }

    if (cfg.verbose) {
        std::cerr << "[INFO] Connection from " << client_ip << " closed" << std::endl;
    }
}

int main(int argc, char** argv) {
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);

    ProxyConfig cfg;
    if (!parse_args(argc, argv, cfg)) {
        usage(argv[0]);
        return 1;
    }

    std::cerr << "[CONFIG] Listen: " << cfg.listen_host << ":" << cfg.listen_port << std::endl;
    std::cerr << "[CONFIG] Upstream: " << cfg.upstream_host << ":" << cfg.upstream_port << std::endl;
    std::cerr << "[CONFIG] Latency: " << cfg.latency_ms << "ms ± " << cfg.jitter_ms 
              << "ms (max: " << cfg.max_latency_ms << "ms)" << std::endl;
    std::cerr << "[CONFIG] Drop rate: " << cfg.drop_rate << ", Dup rate: " << cfg.dup_rate << std::endl;
    std::cerr << "[CONFIG] Direction: " << cfg.direction << ", Bandwidth: " << cfg.bandwidth_kbps << " kbps" << std::endl;
    std::cerr << "[CONFIG] Burst mode: " << (cfg.enable_burst ? "enabled" : "disabled") << std::endl;
    std::cerr << "[CONFIG] HTTP errors: " << (cfg.http_friendly_errors ? "enabled" : "disabled") << std::endl;
    std::cerr << "[CONFIG] RST on upstream connect fail: " << (cfg.rst_on_upstream_connect_fail ? "enabled" : "disabled") << std::endl;
    std::cerr << "[CONFIG] RST on midstream errors: " << (cfg.rst_on_midstream_errors ? "enabled" : "disabled") << std::endl;
    std::cerr << "[CONFIG] Idle timeout: " << cfg.idle_timeout_sec << " seconds" << std::endl;
    std::cerr << "[CONFIG] IPv6-only mode: " << (cfg.v6_only ? "enabled" : "disabled") << std::endl;

    // Validation
    if (cfg.listen_port <= 0 || cfg.listen_port > 65535) {
        std::cerr << "[FATAL] Invalid --listen-port" << std::endl; 
        return 1;
    }
    if (cfg.upstream_port <= 0 || cfg.upstream_port > 65535) {
        std::cerr << "[FATAL] Invalid --upstream-port" << std::endl; 
        return 1;
    }

    try {
        DirectionFlags dirs = parse_direction(cfg.direction);
        if (dirs.both_disabled()) {
            std::cerr << "[FATAL] Both directions disabled - proxy would be useless" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    // FIXED: Use enhanced backlog calculation with SOMAXCONN only
    int backlog = std::min(cfg.max_connections, SOMAXCONN);

    int listen_fd = listen_on(cfg.listen_host, cfg.listen_port, backlog, cfg.v6_only);
    if (listen_fd < 0) {
        std::cerr << "[FATAL] Failed to bind to " << cfg.listen_host << ":" << cfg.listen_port << std::endl;
        return 1;
    }

    std::cerr << "[INFO] Proxy listening on " << cfg.listen_host << ":" << cfg.listen_port 
              << " (backlog: " << backlog << ")" << std::endl;
    
    // Improved dual-stack logging
    if (!cfg.v6_only && (cfg.listen_host == "::" || cfg.listen_host == "0.0.0.0" || cfg.listen_host.empty())) {
        std::cerr << "[INFO] Dual-stack mode active (IPv4-mapped addresses accepted)" << std::endl;
    }

    std::mutex threads_mutex;
    LogRateLimiter backpressure_limiter(std::chrono::milliseconds(5000)); // Rate limit backpressure warnings
    LogRateLimiter upstream_fail_limiter(std::chrono::milliseconds(5000)); // IMPROVED: Rate limit upstream failures
    
    // Thread cleanup loop
    std::thread cleanup_thread([&]() {
        using namespace std::chrono_literals;
        while (g_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(250ms);
            std::lock_guard<std::mutex> lock(threads_mutex);

            for (auto it = connection_threads.begin(); it != connection_threads.end(); ) {
                if (it->done->load(std::memory_order_acquire)) {
                    if (it->th && it->th->joinable()) it->th->join();
                    it = connection_threads.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Final cleanup
        std::lock_guard<std::mutex> lock(threads_mutex);
        for (auto &h : connection_threads) {
            if (h.th && h.th->joinable()) h.th->join();
        }
        connection_threads.clear();
    });
  
    // Main accept loop with enhanced error handling
    while (g_running.load()) {
        sockaddr_storage client_addr{};
        socklen_t client_len = sizeof(client_addr);

        #ifdef HAVE_ACCEPT4
            int client_fd = accept4(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len, 
                                   SOCK_CLOEXEC | SOCK_NONBLOCK);
        #else
            int client_fd = accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd >= 0) {
                int fd_flags = fcntl(client_fd, F_GETFD);
                if (fd_flags >= 0) {
                    fcntl(client_fd, F_SETFD, fd_flags | FD_CLOEXEC);
                } else {
                    std::cerr << "[ERROR] fcntl F_GETFD failed: " << categorize_errno(errno) << std::endl;
                    ::close(client_fd);
                    continue;
                }
                
                int fl_flags = fcntl(client_fd, F_GETFL, 0);
                if (fl_flags >= 0) {
                    fcntl(client_fd, F_SETFL, fl_flags | O_NONBLOCK);
                } else {
                    std::cerr << "[ERROR] fcntl F_GETFL failed: " << categorize_errno(errno) << std::endl;
                    ::close(client_fd);
                    continue;
                }
            }
        #endif

        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            if (!g_running.load()) break;
            std::cerr << "[ERROR] accept failed: " << categorize_errno(errno) << std::endl;
            continue;
        }

        // Connection limiting with backpressure
        size_t current_connections = 0;
        int sleep_ms = 0;

        {
            std::lock_guard<std::mutex> lock(threads_mutex);
            current_connections = connection_threads.size();

            if (current_connections >= static_cast<size_t>(cfg.max_connections)) {
                if (backpressure_limiter.should_log()) {
                    std::cerr << "[WARN] Max connections (" << cfg.max_connections 
                              << ") reached, rejecting new connections" << std::endl;
                }
                
                // Try HTTP-friendly rejection with timeout protection
                if (cfg.http_friendly_errors) {
                    char peek_buf[32] = {};
                    ssize_t peeked = 0;
                    if (try_http_peek(client_fd, peek_buf, sizeof(peek_buf), peeked)) {
                        if (peeked > 0 && looks_like_http(peek_buf, static_cast<size_t>(peeked))) {
                            send_429(client_fd);
                            ::shutdown(client_fd, SHUT_WR);
                            
                            // Brief drain using poll
                            drain_with_poll(client_fd, 50);
                        }
                    }
                }
                
                // Apply appropriate close method based on config
                if (cfg.rst_on_midstream_errors) {
                    rst_close(client_fd);
                } else {
                    ::close(client_fd);
                }
                continue;
            }

            // Adaptive backpressure with clamping
            if (current_connections + 4 >= static_cast<size_t>(cfg.max_connections)) {
                int cur = g_accept_backpressure_ms.load(std::memory_order_relaxed);
                int next = std::min(cur + 5, kMaxRetryDelay);
                g_accept_backpressure_ms.store(next, std::memory_order_relaxed);
                sleep_ms = next;
            } else {
                int cur = g_accept_backpressure_ms.load(std::memory_order_relaxed);
                if (cur > 0) g_accept_backpressure_ms.store(cur - 1, std::memory_order_relaxed);
            }
        }

        if (sleep_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        }

        set_socket_options(client_fd, cfg.verbose);

        // Spawn connection handler
        {
            std::lock_guard<std::mutex> lock(threads_mutex);

            // Double-check connection limit after backpressure
            if (connection_threads.size() >= static_cast<size_t>(cfg.max_connections)) {
                if (backpressure_limiter.should_log()) {
                    std::cerr << "[WARN] Connection limit reached after backpressure, dropping" << std::endl;
                }
                rst_close(client_fd);
                continue;
            }

            auto done = std::make_shared<std::atomic<bool>>(false);
            auto th = std::make_unique<std::thread>([client_fd, cfg, done]() {
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

    std::cerr << "[INFO] Proxy shutdown complete" << std::endl;
    return 0;
}
