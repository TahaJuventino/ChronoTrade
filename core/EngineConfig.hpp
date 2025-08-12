#pragma once

#include "../security/SecurityAwareLogger.hpp"

namespace engine {

    struct EngineConfig {
        static inline bool allow_duplicate_timestamps = false; // Phase 1 default: strict
    };

}