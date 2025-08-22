cmake_minimum_required(VERSION 3.15)
project(ChronoTrade)

# --- Options / toggles ---
option(ENABLE_WERROR "Treat warnings as errors" ON)
option(BUILD_TESTS  "Build unit tests"          ON)
option(BUILD_TOOLS  "Build simulators/proxies"  ON)

# === FAST BUILD INFRA ===

# Warn if WSL builds in /mnt/c (slow disk IO)
if(UNIX AND CMAKE_SOURCE_DIR MATCHES "/mnt/c")
  message(WARNING "⚠️ Building from /mnt/c in WSL is slow. Move project to ~/ for fast compile.")
endif()

# Enable Ninja + ccache
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  message(STATUS "🚀 ccache enabled")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_UNITY_BUILD ON CACHE BOOL "Faster incremental builds" FORCE)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Security options
option(ENABLE_ASAN  "Enable AddressSanitizer"           OFF)
option(ENABLE_TSAN  "Enable ThreadSanitizer"            OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_CFI   "Enable Control Flow Integrity"      OFF)

find_package(Threads REQUIRED)
find_package(GTest   REQUIRED)
find_package(OpenSSL REQUIRED)

# ========== GIT COMMIT HASH INJECTION ==========
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT GIT_COMMIT_HASH)
  set(GIT_COMMIT_HASH "unknown")
endif()
add_compile_definitions(BUILD_HASH="${GIT_COMMIT_HASH}")

# ========== COMPILER/LINKER FLAGS ==========
set(HARDENING_FLAGS "")
if (MSVC)
  # MSVC equivalents
  list(APPEND HARDENING_FLAGS
    /W4 /permissive- /EHsc
    /guard:cf         # Control Flow Guard
    /sdl              # Additional security checks
    /GS               # Buffer security check
    /utf-8
  )
  if (ENABLE_WERROR)
    list(APPEND HARDENING_FLAGS /WX)
  endif()
else()
  # GCC/Clang
  list(APPEND HARDENING_FLAGS
    -Wall -Wextra -pedantic
    -Wformat=2 -Wformat-security
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=3
    -fcf-protection=full
    -fstack-clash-protection
    -fno-omit-frame-pointer
  )
  if (ENABLE_WERROR)
    list(APPEND HARDENING_FLAGS -Werror)
  endif()
  # Linker hardening (ELF)
  if (UNIX AND NOT APPLE)
    add_link_options(-Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack -pie)
  endif()
endif()

# ========== MAIN APP ==========
file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "core/*.cpp" "engine/*.cpp"
    "feed/*.cpp" "feed/*/*.cpp"
    "threads/*.cpp" "plot/*.cpp" "strategy/*.cpp"
    "event/*.cpp" "security/*.cpp" "utils/*.cpp"
)
# Remove simulator and proxy from main build
list(FILTER CORE_SRC EXCLUDE REGEX "feed/simulators/.*")
list(FILTER CORE_SRC EXCLUDE REGEX "feed/TCPLatencyProxy.cpp")

add_executable(ChronoTrade ${CORE_SRC} main.cpp)
target_compile_options(ChronoTrade PRIVATE ${HARDENING_FLAGS})
target_link_libraries(ChronoTrade PRIVATE Threads::Threads OpenSSL::Crypto)
target_include_directories(ChronoTrade PRIVATE
  ${PROJECT_SOURCE_DIR}/feed
  ${PROJECT_SOURCE_DIR}/core
  ${PROJECT_SOURCE_DIR}/utils
  ${PROJECT_SOURCE_DIR}/security
  ${PROJECT_SOURCE_DIR}/observability
  ${PROJECT_SOURCE_DIR}/threads
)
add_compile_definitions(ENABLE_LOGS=0) # Disable logs by default

# ========== TESTS ==========
if (BUILD_TESTS)
  enable_testing()
  add_compile_definitions(PANIC_THROWS_IN_TESTS=1)

  # Helper to define & register a gtest binary with CTest
  function(add_test_target name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE GTest::gtest_main OpenSSL::Crypto)
    target_include_directories(${name} PRIVATE 
      ${PROJECT_SOURCE_DIR}
      ${PROJECT_SOURCE_DIR}/core
      ${PROJECT_SOURCE_DIR}/engine
      ${PROJECT_SOURCE_DIR}/feed
      ${PROJECT_SOURCE_DIR}/utils
      ${PROJECT_SOURCE_DIR}/external
    )
    target_compile_options(${name} PRIVATE ${HARDENING_FLAGS})
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  # Core tests
  add_test_target(test_order tests/test_order.cpp)
  add_test_target(test_csv_parser tests/test_csv_parser.cpp)
  add_test_target(test_candlestick tests/test_candlestick.cpp)
  add_test_target(test_orderbook tests/test_orderbook.cpp)
  add_test_target(test_candlestick_generator tests/test_candlestick_generator.cpp)
  add_test_target(test_sma_indicator tests/test_sma_indicator.cpp)
  add_test_target(test_fixed_window tests/test_fixed_window.cpp)
  add_test_target(test_rsi_indicator tests/test_rsi_indicator.cpp)
  add_test_target(test_macd_indicator tests/test_macd_indicator.cpp)
  add_test_target(test_bollinger_indicator tests/test_bollinger_indicator.cpp)
  add_test_target(test_indicator_registry tests/test_indicator_registry.cpp)
  add_test_target(test_loop_processor tests/test_loop_processor.cpp)
  add_test_target(test_arena_allocator tests/test_arena_allocator.cpp)
  add_test_target(test_simd_sort tests/test_simd_sort.cpp)
  add_test_target(test_orderbook_arena tests/test_orderbook_arena.cpp)
  add_test_target(test_iindicator_mock tests/test_iindicator_mock.cpp)
  add_test_target(test_csv_feedsource tests/test_csv_feedsource.cpp)
  add_test_target(test_indicators tests/test_indicators.cpp)
  add_test_target(test_simd_sort_benchmark tests/test_simd_sort_benchmark.cpp)
  add_test_target(test_feed_redteam_injection tests/test_feed_redteam_injection.cpp)
  add_test_target(test_feed_hashlogger tests/test_feed_hashlogger.cpp)
  add_test_target(test_security_macros tests/test_security_macros.cpp)
  add_test_target(test_zero_copy_csv_parser tests/test_zero_copy_csv_parser.cpp)

  # Special test configurations
  target_compile_options(test_orderbook_arena PRIVATE -fexceptions)
  target_link_libraries(test_indicators PRIVATE Threads::Threads)

  # Feed Manager test (has unused variable warning)
  add_test_target(test_feed_manager tests/test_feed_manager.cpp)
  target_compile_options(test_feed_manager PRIVATE -Wno-error=unused-variable)

  # Zero-copy parser hardened build
  if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(test_zero_copy_csv_parser PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g3)
    target_link_options(test_zero_copy_csv_parser PRIVATE -fsanitize=address,undefined)
  endif()

  # Unix-specific tests
  if(UNIX AND NOT WIN32)
    add_test_target(test_socket_feedsource tests/test_socket_feedsource.cpp)
  endif()
  if(UNIX)
    add_test_target(test_shm_feedsource tests/test_shm_feedsource.cpp)
    target_link_libraries(test_shm_feedsource PRIVATE Threads::Threads)
  endif()

  # TCP Latency Proxy test
  add_executable(test_tcp_latency_proxy tests/test_tcp_latency_proxy.cpp)
  target_link_libraries(test_tcp_latency_proxy PRIVATE GTest::gtest_main Threads::Threads)
  target_include_directories(test_tcp_latency_proxy PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/utils
  )
  target_compile_options(test_tcp_latency_proxy PRIVATE ${HARDENING_FLAGS})
  add_test(NAME test_tcp_latency_proxy COMMAND test_tcp_latency_proxy)

  # ========== ENSURE CRYPTO LINKING FOR REMAINING TESTS ==========
  # Force OpenSSL::Crypto for tests that need CryptoHasher/SHA256 but may have lost it
  set(TESTS_NEED_CRYPTO
    test_loop_processor
    test_simd_sort
    test_bollinger_indicator
    test_indicator_registry
    test_orderbook
    test_orderbook_arena
    test_candlestick_generator
  )

  foreach(tgt IN LISTS TESTS_NEED_CRYPTO)
    if (TARGET ${tgt})
      target_link_libraries(${tgt} PRIVATE OpenSSL::Crypto)
      message(STATUS "Force-linked OpenSSL::Crypto to ${tgt}")
    endif()
  endforeach()
endif() # BUILD_TESTS

# ========== SIMULATORS & UTILITIES ==========
if (BUILD_TOOLS)
  add_executable(shm_writer_simulator feed/simulators/shm_writer_simulator.cpp)
  target_include_directories(shm_writer_simulator PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/core
  )

  add_executable(tcp_latency_proxy feed/TCPLatencyProxy.cpp)
  target_link_libraries(tcp_latency_proxy PRIVATE Threads::Threads)
  target_include_directories(tcp_latency_proxy PRIVATE 
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/utils
  )
  target_compile_options(shm_writer_simulator PRIVATE ${HARDENING_FLAGS})
  target_compile_options(tcp_latency_proxy     PRIVATE ${HARDENING_FLAGS})
endif()

# ========== PROJECT-WIDE SANITIZER APPLICATION ==========
get_property(ALL_TARGETS DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
foreach(tgt ${ALL_TARGETS})
  get_target_property(tgt_type ${tgt} TYPE)
  if(NOT tgt_type STREQUAL "INTERFACE_LIBRARY")
    
    if(ENABLE_ASAN)
      target_compile_options(${tgt} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
      target_link_options(${tgt} PRIVATE -fsanitize=address)
    endif()
    if(ENABLE_TSAN)
      target_compile_options(${tgt} PRIVATE -fsanitize=thread)
      target_link_options(${tgt} PRIVATE -fsanitize=thread)
    endif()
    if(ENABLE_UBSAN)
      target_compile_options(${tgt} PRIVATE -fsanitize=undefined)
      target_link_options(${tgt} PRIVATE -fsanitize=undefined)
    endif()
    if(ENABLE_CFI AND CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_BUILD_TYPE MATCHES "Release")
      target_compile_options(${tgt} PRIVATE -fsanitize=cfi -fvisibility=hidden -flto)
      target_link_options(${tgt} PRIVATE -fsanitize=cfi -fvisibility=hidden -flto)
    endif()
  endif()
endforeach()

# ========== BUILD VALIDATION & STATUS ==========
message(STATUS "")
message(STATUS "=== ChronoTrade Build Configuration ===")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Git Hash: ${GIT_COMMIT_HASH}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
message(STATUS "Build Options:")
message(STATUS "  BUILD_TESTS: ${BUILD_TESTS}")
message(STATUS "  BUILD_TOOLS: ${BUILD_TOOLS}")
message(STATUS "  ENABLE_WERROR: ${ENABLE_WERROR}")
message(STATUS "  Unity builds: ${CMAKE_UNITY_BUILD}")
message(STATUS "")
message(STATUS "Security Configuration:")
message(STATUS "  ASAN: ${ENABLE_ASAN}")
message(STATUS "  TSAN: ${ENABLE_TSAN}")  
message(STATUS "  UBSAN: ${ENABLE_UBSAN}")
message(STATUS "  CFI: ${ENABLE_CFI}")
if(MSVC)
  message(STATUS "  MSVC: /guard:cf /sdl /GS")
else()
  message(STATUS "  GCC/Clang: FORTIFY_SOURCE=3, stack protection, CFI")
  if(UNIX AND NOT APPLE)
    message(STATUS "  Linker: RELRO, NOW, noexecstack, PIE")
  endif()
endif()
message(STATUS "")
if(CCACHE_PROGRAM)
  message(STATUS "Acceleration: ccache enabled")
else()
  message(STATUS "Acceleration: ccache not found (install for faster rebuilds)")
endif()
message(STATUS "")

# ========== CUSTOM TARGETS ==========
if(BUILD_TESTS)
  # Fast test runner (core tests only)
  add_custom_target(test-fast
    COMMAND ${CMAKE_CTEST_COMMAND} -R "test_(order|candlestick|orderbook|sma|rsi)$$" --output-on-failure
    DEPENDS test_order test_candlestick test_orderbook test_sma_indicator test_rsi_indicator
    COMMENT "Running core tests (fast subset)"
  )

  # Security-focused test runner
  add_custom_target(test-security
    COMMAND ${CMAKE_CTEST_COMMAND} -R "test_(security|feed_redteam|zero_copy)" --output-on-failure
    DEPENDS test_security_macros test_feed_redteam_injection test_zero_copy_csv_parser
    COMMENT "Running security-focused tests"
  )

  # All tests runner
  add_custom_target(test-all
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all registered tests"
  )
endif()

# Clean build artifacts
add_custom_target(clean-all
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/CMakeFiles
  COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_BINARY_DIR}/CMakeCache.txt
  COMMENT "Deep clean of build artifacts"
)