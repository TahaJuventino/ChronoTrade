#include "utils/logger.h"

void verify_build_flags() {
#if !defined(__OPTIMIZE__)
    LOG(LOG_WARN);
    std::cerr << "[WARN] Built without optimizations (-O2)\n";
#endif
#if !defined(__GNUC__)
    PANIC("Unsupported compiler");
#endif
#ifdef __SANITIZE_ADDRESS__
    LOG(INFO);
    std::cerr << "AddressSanitizer is enabled.\n";
#endif
}

int main() {
    verify_build_flags();  // Audit build environment

    LOG(LOG_INFO);

    std::cerr << "ChronoTrade system initialized.\n";

    std::cerr << "Build hash: " << BUILD_HASH << std::endl;

    // Uncomment for panic test:
    // PANIC("Intentional crash test");

    return 0;
}
