cmake_minimum_required(VERSION 3.15)
project(ChronoTrade)

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
    "core/*.cpp" "engine/*.cpp" "feed/*.cpp"
    "threads/*.cpp" "plot/*.cpp" "strategy/*.cpp"
    "event/*.cpp" "security/*.cpp" "utils/*.cpp"
)

add_executable(ChronoTrade ${CORE_SRC} main.cpp)

target_compile_options(ChronoTrade PRIVATE
  -Wall -Wextra -Werror -pedantic
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
  -fno-omit-frame-pointer
)

target_link_libraries(ChronoTrade PRIVATE Threads::Threads)

target_include_directories(ChronoTrade PRIVATE
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
target_include_directories(test_order PRIVATE ${PROJECT_SOURCE_DIR}/core)

# ---- Feed Parser Tests ----
add_executable(test_csv_parser tests/test_csv_parser.cpp)
target_link_libraries(test_csv_parser PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_csv_parser PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/feed)

# ---- Candlestick Core + Advanced ----
add_executable(test_candlestick tests/test_candlestick.cpp)
target_link_libraries(test_candlestick PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_candlestick PRIVATE ${PROJECT_SOURCE_DIR}/core)

# ---- OrderBook Threaded Container ----
add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_order_book PRIVATE ${PROJECT_SOURCE_DIR}/core)

# ---- Candle Generator Sliding Window ----
add_executable(test_candlestick_generator tests/test_candlestick_generator.cpp)
target_link_libraries(test_candlestick_generator PRIVATE GTest::gtest GTest::gtest_main)
target_include_directories(test_candlestick_generator PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine)

## ---- SMA Indicator Tests ----
add_executable(test_sma_indicator tests/test_sma_indicator.cpp)
target_link_libraries(test_sma_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_sma_indicator PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine ${PROJECT_SOURCE_DIR}/utils)

# ---- Fixed Window Tests ----
add_executable(test_fixed_window tests/test_fixed_window.cpp)
target_link_libraries(test_fixed_window PRIVATE GTest::gtest_main)
target_include_directories(test_fixed_window PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine)

# ---- RSI Indicator Tests ----
add_executable(test_rsi_indicator tests/test_rsi_indicator.cpp)
target_link_libraries(test_rsi_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_rsi_indicator PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine ${PROJECT_SOURCE_DIR}/utils)

# ---- MACD Indicator Tests ----
add_executable(test_macd_indicator tests/test_macd_indicator.cpp)
target_link_libraries(test_macd_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_macd_indicator PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine)

# ---- Bollinger Bands Tests ----
add_executable(test_bollinger_indicator tests/test_bollinger_indicator.cpp)
target_link_libraries(test_bollinger_indicator PRIVATE GTest::gtest_main)
target_include_directories(test_bollinger_indicator PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine ${PROJECT_SOURCE_DIR}/utils)

# ---- Indicator Registry Tests ----
add_executable(test_indicator_registry tests/test_indicator_registry.cpp)
target_link_libraries(test_indicator_registry PRIVATE GTest::gtest_main)
target_include_directories(test_indicator_registry PRIVATE engine core)

# ---- Loop Processor Tests ----
add_executable(test_loop_processor tests/test_loop_processor.cpp)
target_link_libraries(test_loop_processor PRIVATE GTest::gtest_main)
target_include_directories(test_loop_processor PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine)

# ---- Arena Allocator Tests ----
add_executable(test_arena_allocator tests/test_arena_allocator.cpp)
target_link_libraries(test_arena_allocator PRIVATE GTest::gtest_main)
target_include_directories(test_arena_allocator PRIVATE ${PROJECT_SOURCE_DIR}/utils)

# ---- SIMD Sort Tests ----
add_executable(test_simd_sort tests/test_simd_sort.cpp)
target_link_libraries(test_simd_sort PRIVATE GTest::gtest_main)
target_include_directories(test_simd_sort PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/utils)

# ---- OrderBook Arena Tests ----
add_executable(test_orderbook_arena tests/test_orderbook_arena.cpp)
target_link_libraries(test_orderbook_arena PRIVATE GTest::gtest_main)
target_include_directories(test_orderbook_arena PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/utils)
target_compile_options(test_orderbook_arena PRIVATE -fexceptions)

# ---- IIndicator Mock Tests ----
add_executable(test_iindicator_mock tests/test_iindicator_mock.cpp)
target_link_libraries(test_iindicator_mock PRIVATE GTest::gtest_main)
target_include_directories(test_iindicator_mock PRIVATE ${PROJECT_SOURCE_DIR}/core ${PROJECT_SOURCE_DIR}/engine)