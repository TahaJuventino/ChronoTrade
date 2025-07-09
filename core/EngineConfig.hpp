#pragma once
#include "../utils/logger.h"

// Optional logging toggle (if you later want to log config changes or access)
#define ENABLE_LOGS 0

#if ENABLE_LOGS
#define SAFE_LOG(level) LOG(level)
#else
#define SAFE_LOG(level) if (false) LOG(level)
#endif

struct EngineConfig {
    static inline bool allow_duplicate_timestamps = false; // Phase 1 default: strict
};
