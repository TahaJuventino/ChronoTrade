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
)

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

