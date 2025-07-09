#pragma once
#include <string>
#include <stdexcept>
#include "../utils/logger.h"

// Optional logging toggle
#define ENABLE_AUTHFLAG_LOG 0

#if ENABLE_AUTHFLAG_LOG
#define AUTH_LOG(level) LOG(level)
#else
#define AUTH_LOG(level) if (false) LOG(level)
#endif

enum class AuthFlags {
    TRUSTED,
    UNVERIFIED,
    MALFORMED,
    SUSPICIOUS
};

inline std::string to_string(AuthFlags flag) {
    switch (flag) {
        case AuthFlags::TRUSTED: return "TRUSTED";
        case AuthFlags::UNVERIFIED: return "UNVERIFIED";
        case AuthFlags::MALFORMED: return "MALFORMED";
        case AuthFlags::SUSPICIOUS: return "SUSPICIOUS";
        default: return "UNKNOWN";
    }
}

inline AuthFlags from_string(const std::string& str) {
    if (str == "TRUSTED") return AuthFlags::TRUSTED;
    if (str == "UNVERIFIED") return AuthFlags::UNVERIFIED;
    if (str == "MALFORMED") return AuthFlags::MALFORMED;
    if (str == "SUSPICIOUS") return AuthFlags::SUSPICIOUS;

    AUTH_LOG(WARN) << "[Invalid AuthFlags] Input: " << str;
    throw std::invalid_argument("Invalid AuthFlags string: " + str);
}
