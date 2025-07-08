#pragma once
#include <string>

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
    throw std::invalid_argument("Invalid AuthFlags string: " + str);
}