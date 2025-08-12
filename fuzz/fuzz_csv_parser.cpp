#include <cstddef>
#include <cstdint>
#include <string>
#include "../feed/CSVOrderParser.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    feed::CSVOrderParser parser;
    std::string input(reinterpret_cast<const char*>(data), size);
    auto result = parser.parse(input);
    (void)result;
    return 0;
}
