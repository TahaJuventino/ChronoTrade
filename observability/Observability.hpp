#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

#include "../security/SecurityAwareLogger.hpp"

namespace observability {

class Observability {
public:
    static Observability &instance() {
        static Observability inst;
        return inst;
    }

    void incrementMetric(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++metrics_[name];
    }

    uint64_t getMetric(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex_);
        return metrics_[name];
    }

    template <typename Fn>
    void trace(const std::string &span, Fn &&fn) {
        fn();
        security::SecurityAwareLogger::instance().log(security::SecurityAwareLogger::Level::Info,
                                                     "[Trace] %s", span.c_str());
    }

    bool validateLogIntegrity(const std::string &line) {
        // Placeholder for actual validation logic.
        security::SecurityAwareLogger::instance().log(security::SecurityAwareLogger::Level::Info,
                                                     "[Validate] %s", line.c_str());
        return true;
    }

private:
    std::mutex mutex_;
    std::map<std::string, uint64_t> metrics_;
};

} // namespace observability

