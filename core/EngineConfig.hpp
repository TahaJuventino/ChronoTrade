#pragma once

#include "../utils/logger.h"

#define INFO 1
#define WARN 2
#define ERROR 3

namespace engine {

    struct EngineConfig {
        static inline bool allow_duplicate_timestamps = false; // Phase 1 default: strict
    };

}