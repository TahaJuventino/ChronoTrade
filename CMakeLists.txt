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
    "infrastructure/*.cpp" "infrastructure/*/*.cpp"
    "plot/*.cpp" "strategy/*.cpp"
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
  ${PROJECT_SOURCE_DIR}/engine
  ${PROJECT_SOURCE_DIR}/interfaces
  ${PROJECT_SOURCE_DIR}/infrastructure
  ${PROJECT_SOURCE_DIR}/observability
  ${PROJECT_SOURCE_DIR}/utils
  ${PROJECT_SOURCE_DIR}/security
)

add_compile_definitions(ENABLE_LOGS=0) # Disable logs by default

# ========== GTEST-BASED TESTS ==========
find_package(GTest REQUIRED)
find_package(OpenSSL REQUIRED)

file(GLOB_RECURSE TEST_SOURCES "tests/**/*.cpp")
foreach(test_src ${TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_link_libraries(${test_name} PRIVATE GTest::gtest GTest::gtest_main OpenSSL::Crypto Threads::Threads)
  target_include_directories(${test_name} PRIVATE ${PROJECT_SOURCE_DIR})
endforeach()
