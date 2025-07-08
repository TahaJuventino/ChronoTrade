cmake_minimum_required(VERSION 3.15)
project(ChronoTrade)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

# Git commit hash injection (must come early)
execute_process(
  COMMAND git rev-parse --short HEAD
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_compile_definitions(BUILD_HASH="${GIT_COMMIT_HASH}")
add_compile_definitions(BUILD_HASH=\"${GIT_COMMIT_HASH}\")

# Gather sources
file(GLOB_RECURSE SRC_FILES CONFIGURE_DEPENDS
    "core/*.cpp" "engine/*.cpp" "feed/*.cpp"
    "threads/*.cpp" "plot/*.cpp" "strategy/*.cpp"
    "event/*.cpp" "security/*.cpp" "tests/*.cpp"
    "utils/*.cpp" "main.cpp"
)

# Define target
add_executable(ChronoTrade ${SRC_FILES})

# Compile flags
target_compile_options(ChronoTrade PRIVATE
  -Wall -Wextra -Werror -pedantic
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
  -fno-omit-frame-pointer
)

# Link libraries
target_link_libraries(ChronoTrade PRIVATE Threads::Threads)

# Include paths
target_include_directories(ChronoTrade PRIVATE
  ${PROJECT_SOURCE_DIR}/core
  ${PROJECT_SOURCE_DIR}/utils
  ${PROJECT_SOURCE_DIR}/security
)
