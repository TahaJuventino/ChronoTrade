add_executable(fuzz_csv_parser fuzz_csv_parser.cpp)

target_include_directories(fuzz_csv_parser PRIVATE
    ${PROJECT_SOURCE_DIR}/feed
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/utils
)

target_compile_options(fuzz_csv_parser PRIVATE -fsanitize=fuzzer,address)
target_link_options(fuzz_csv_parser PRIVATE -fsanitize=fuzzer,address)
