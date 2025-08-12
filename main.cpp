#include "security/SecurityAwareLogger.hpp"
#include "observability/Observability.hpp"
#include "utils/Panic.hpp"

void verify_build_flags() {
#if !defined(__OPTIMIZE__)
    security::SecurityAwareLogger::instance().log(
        security::SecurityAwareLogger::Level::Warn,
        "Built without optimizations (-O2)");
#endif
#if !defined(__GNUC__)
    PANIC("Unsupported compiler");
#endif
#ifdef __SANITIZE_ADDRESS__
    security::SecurityAwareLogger::instance().log(
        security::SecurityAwareLogger::Level::Info,
        "AddressSanitizer is enabled.");
#endif
}

int main() {
    verify_build_flags();  // Audit build environment

    security::SecurityAwareLogger::instance().log(
        security::SecurityAwareLogger::Level::Info,
        "ChronoTrade system initialized.");

    observability::Observability::instance().trace("startup", [] {});

    std::cerr << "Build hash: " << BUILD_HASH << std::endl;

    // Uncomment for panic test:
    // PANIC("Intentional crash test");

    return 0;
}
