cmake_minimum_required(VERSION 3.15)
project(ChronoTrade)

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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# ========== GIT COMMIT HASH INJECTION ==========

execute_process(
  COMMAND git rev-parse --short HEAD
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

add_compile_definitions(BUILD_HASH="${GIT_COMMIT_HASH}")

# ========== MAIN APP ==========

file(GLOB_RECURSE CORE_SRC CONFIGURE_DEPENDS
    "core/*.cpp" "engine/*.cpp"
    "feed/*.cpp" "feed/*/*.cpp"
    "threads/*.cpp" "plot/*.cpp" "strategy/*.cpp"
    "event/*.cpp" "security/*.cpp" "utils/*.cpp"
)

# ⛔️ REMOVE simulator from ChronoTrade build
list(FILTER CORE_SRC EXCLUDE REGEX "feed/simulators/.*")
list(FILTER CORE_SRC EXCLUDE REGEX "feed/TCPLatencyProxy.cpp")

add_executable(ChronoTrade ${CORE_SRC} main.cpp)

target_compile_options(ChronoTrade PRIVATE
  -Wall -Wextra -Werror -pedantic
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
  -fno-omit-frame-pointer
)

target_link_libraries(ChronoTrade PRIVATE Threads::Threads)

target_include_directories(ChronoTrade PRIVATE
  ${PROJECT_SOURCE_DIR}/feed
  ${PROJECT_SOURCE_DIR}/core
  ${PROJECT_SOURCE_DIR}/utils
  ${PROJECT_SOURCE_DIR}/security
  ${PROJECT_SOURCE_DIR}/threads
)

add_compile_definitions(ENABLE_LOGS=0) # Disable logs by default

# ========== GTEST-BASED TESTS ==========

# Find GoogleTest if available (MSYS2: pacman -S mingw-w64-x86_64-gtest)
find_package(GTest REQUIRED)

# ---- Order Tests ----
add_executable(test_order tests/test_order.cpp)
target_link_libraries(test_order PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_order PRIVATE 
    ${PROJECT_SOURCE_DIR}/core
)

# ---- Feed Parser Tests ----
add_executable(test_csv_parser tests/test_csv_parser.cpp)
target_link_libraries(test_csv_parser PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_csv_parser PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/feed
)

# ---- Candlestick Core + Advanced ----
add_executable(test_candlestick tests/test_candlestick.cpp)
target_link_libraries(test_candlestick PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_candlestick PRIVATE 
    ${PROJECT_SOURCE_DIR}/core
)

# ---- OrderBook Threaded Container ----
add_executable(test_orderbook tests/test_orderbook.cpp)
target_link_libraries(test_orderbook PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_orderbook PRIVATE 
    ${PROJECT_SOURCE_DIR}/core
)

# ---- Candle Generator Sliding Window ----
add_executable(test_candlestick_generator tests/test_candlestick_generator.cpp)
target_link_libraries(test_candlestick_generator PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_candlestick_generator PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine
)

## ---- SMA Indicator Tests ----
add_executable(test_sma_indicator tests/test_sma_indicator.cpp)
target_link_libraries(test_sma_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_sma_indicator PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine 
    ${PROJECT_SOURCE_DIR}/utils
)

# ---- Fixed Window Tests ----
add_executable(test_fixed_window tests/test_fixed_window.cpp)
target_link_libraries(test_fixed_window PRIVATE GTest::gtest_main)
target_include_directories(test_fixed_window PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine
)

# ---- RSI Indicator Tests ----
add_executable(test_rsi_indicator tests/test_rsi_indicator.cpp)
target_link_libraries(test_rsi_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_rsi_indicator PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine 
    ${PROJECT_SOURCE_DIR}/utils
)

# ---- MACD Indicator Tests ----
add_executable(test_macd_indicator tests/test_macd_indicator.cpp)
target_link_libraries(test_macd_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_macd_indicator PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine
)

# ---- Bollinger Bands Tests ----
add_executable(test_bollinger_indicator tests/test_bollinger_indicator.cpp)
target_link_libraries(test_bollinger_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_bollinger_indicator PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine 
    ${PROJECT_SOURCE_DIR}/utils
)

# ---- Indicator Registry Tests ----
add_executable(test_indicator_registry tests/test_indicator_registry.cpp)
target_link_libraries(test_indicator_registry PRIVATE GTest::gtest_main)
target_include_directories(test_indicator_registry PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine
)

# ---- Loop Processor Tests ----
add_executable(test_loop_processor tests/test_loop_processor.cpp)
target_link_libraries(test_loop_processor PRIVATE GTest::gtest_main)
target_include_directories(test_loop_processor PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine
)

# ---- Arena Allocator Tests ----
add_executable(test_arena_allocator tests/test_arena_allocator.cpp)
target_link_libraries(test_arena_allocator PRIVATE GTest::gtest_main)
target_include_directories(test_arena_allocator PRIVATE 
    ${PROJECT_SOURCE_DIR}/utils
)

# ---- SIMD Sort Tests ----
add_executable(test_simd_sort tests/test_simd_sort.cpp)
target_link_libraries(test_simd_sort PRIVATE GTest::gtest_main)
target_include_directories(test_simd_sort PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/utils
)

# ---- OrderBook Arena Tests ----
add_executable(test_orderbook_arena tests/test_orderbook_arena.cpp)
target_link_libraries(test_orderbook_arena PRIVATE GTest::gtest_main)
target_include_directories(test_orderbook_arena PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/utils
)
target_compile_options(test_orderbook_arena PRIVATE -fexceptions)

# ---- IIndicator Mock Tests ----
add_executable(test_iindicator_mock tests/test_iindicator_mock.cpp)
target_link_libraries(test_iindicator_mock PRIVATE GTest::gtest_main)
target_include_directories(test_iindicator_mock PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/engine
)

# ---- Feed Manager Tests ----
add_executable(test_feed_manager tests/test_feed_manager.cpp)

target_include_directories(test_feed_manager PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/external
)

target_link_libraries(test_feed_manager 
  PRIVATE 
    GTest::gtest_main
    OpenSSL::Crypto
)


# ---- CSV FeedSource Tests ----
add_executable(test_csv_feedsource tests/test_csv_feedsource.cpp)

target_include_directories(test_csv_feedsource PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/external
)

target_link_libraries(test_csv_feedsource 
  PRIVATE 
    GTest::gtest_main
    OpenSSL::Crypto
)


# ---- Indicator Tests ----
add_executable(test_indicators tests/test_indicators.cpp)
target_link_libraries(test_indicators gtest gtest_main pthread)
target_include_directories(test_indicators PRIVATE 
    ${PROJECT_SOURCE_DIR}
)

# ---- SIMD Sort Benchmark Tests ----
add_executable(test_simd_sort_benchmark tests/test_simd_sort_benchmark.cpp)
target_link_libraries(test_simd_sort_benchmark PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_simd_sort_benchmark PRIVATE 
    ${PROJECT_SOURCE_DIR}/core 
    ${PROJECT_SOURCE_DIR}/utils
    )

# ---- Feed Injector Tests ----
add_executable(test_feed_redteam_injection tests/test_feed_redteam_injection.cpp)

target_include_directories(test_feed_redteam_injection PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/external
)

target_link_libraries(test_feed_redteam_injection 
  PRIVATE 
    GTest::gtest_main
    OpenSSL::Crypto
)

# ---- SocketFeedSource Tests ----
if(UNIX AND NOT WIN32)
  add_executable(test_socket_feedsource tests/test_socket_feedsource.cpp)

  target_link_libraries(test_socket_feedsource PRIVATE GTest::gtest_main)
  target_include_directories(test_socket_feedsource PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/external
  )
endif()

# ---- FeedHashLogger Tests ----
add_executable(test_feed_hashlogger tests/test_feed_hashlogger.cpp)

target_include_directories(test_feed_hashlogger PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/external
)

# Find OpenSSL if available (MSYS2: pacman -S mingw-w64-x86_64-openssl)
find_package(OpenSSL REQUIRED)

target_link_libraries(test_feed_hashlogger 
  PRIVATE 
    GTest::gtest_main
    OpenSSL::Crypto
)

# ---- Shared Memory FeedSource Tests ----
if(UNIX)
  add_executable(test_shm_feedsource tests/test_shm_feedsource.cpp)

  target_include_directories(test_shm_feedsource PRIVATE 
      ${PROJECT_SOURCE_DIR}
      ${PROJECT_SOURCE_DIR}/feed
      ${PROJECT_SOURCE_DIR}/external
  )

  target_link_libraries(test_shm_feedsource 
    PRIVATE 
      GTest::gtest_main
      OpenSSL::Crypto
      Threads::Threads
  )
endif()

# ---- SHM Writer Simulator ----
add_executable(shm_writer_simulator feed/simulators/shm_writer_simulator.cpp)

target_include_directories(shm_writer_simulator PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/core
)

# ---- TCPLatencyProxy ----
add_executable(tcp_latency_proxy feed/TCPLatencyProxy.cpp)
target_link_libraries(tcp_latency_proxy PRIVATE Threads::Threads)
target_include_directories(tcp_latency_proxy PRIVATE 
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/utils
)

# ---- TcpLatencyProxyTest ----
add_executable(test_tcp_latency_proxy tests/test_tcp_latency_proxy.cpp)
target_link_libraries(test_tcp_latency_proxy 
    PRIVATE 
    GTest::gtest_main 
    Threads::Threads
)

# ---- ZeroCopyCsvParser Tests ----
add_executable(test_zero_copy_csv_parser
    tests/test_zero_copy_csv_parser.cpp
)

target_link_libraries(test_zero_copy_csv_parser 
    PRIVATE 
    GTest::gtest_main
)

target_include_directories(test_zero_copy_csv_parser PRIVATE 
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/feed
)

# Enable sanitizers for security testing
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(test_zero_copy_csv_parser PRIVATE 
        -fsanitize=address,undefined
        -fno-omit-frame-pointer
        -g3
    )
    target_link_options(test_zero_copy_csv_parser PRIVATE 
        -fsanitize=address,undefined
    )
endif()

target_include_directories(test_zero_copy_csv_parser PRIVATE 
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/feed
)

target_compile_options(test_zero_copy_csv_parser PRIVATE
    -Wall -Wextra -Werror
    -Wformat-security
    -fstack-protector-strong
    -D_FORTIFY_SOURCE=2
)
# ---- Security Macros Tests ----
add_executable(test_security_macros tests/test_security_macros.cpp)
target_link_libraries(test_security_macros PRIVATE GTest::gtest_main)
target_include_directories(test_security_macros PRIVATE
    ${PROJECT_SOURCE_DIR}/tests
)

